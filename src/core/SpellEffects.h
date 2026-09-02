#pragma once
// NOTE ON THE HEADER/SOURCE SPLIT. This header is included by 15 translation units (directly, and
// transitively via CardDatabase.h / Permanent.h / DecisionProvider.h), so every `inline` body here
// is parsed AND code-generated in all of them -- one edit recompiles the world. The largest COLD
// helpers therefore live in SpellEffects.cpp and are only declared here. "Cold" is load-bearing:
// this build has no LTO, so an out-of-line body can no longer be inlined across TUs. Only helpers
// that run once per RESOLUTION of a specific card or effect may move; the per-cast / per-death /
// per-combat helpers stay inline. See SpellEffects.cpp for the measurement and the rule.
#include "EnvFlags.h"
#include "GameState.h"
#include "ManaPool.h"
#include "GameLogger.h"                // g_reveal_logger: capture scry/dig reveals (real play only)
#include "OpponentDeck.h"               // TakeFromTop: THE mill primitive (sets opponent_decked)
#include "../cards/CardDatabase.h"
#include "../ai/DecisionProviders.h"   // ResolveProvider: route deck decisions through the provider
#include "../ai/EngineFlags.h"         // FrontlineTriggerFirst: shared executor/rollout flag reader
#include "../ai/HeuristicArm.h"        // per-job lever overrides, so ONE pooled batch runs both arms
#include "Trace.h"                     // MTG_TRACE=discard: cleanup-discard tie distribution
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <optional>
#include <unordered_map>
#include <unordered_set>

// ---- Characteristics of a card OUTSIDE the battlefield -----------------------------------------
// DeckLoader::MakePlaceholder builds every library/hand/graveyard Card from its NAME alone, so the
// type / colour / mana-cost masks on such a Card are all EMPTY -- `gy_card.IsLand()` is false for a
// fetchland, `.ManaValue()` is 0 for a 5-drop, `.IsMulticolored()` is false for a gold card. Only a
// BATTLEFIELD Permanent carries real masks (PlayLandFromHand / CastSpell copy `def.card`). So any
// characteristic read on a hand/library/graveyard card MUST go through the database definition;
// this is the single accessor for that. Falls back to the card itself for a synthesized identity
// with no DB entry (tokens), which does carry its own masks.
//
// This bit every graveyard-interaction card added for FiveColour: Deathrite's three abilities never
// fired (a self-sacrificed fetchland in the graveyard read as a non-land, so the fuel gate was
// always 0), Garth's Regrowth always took graveyard slot 0 (every MV read as 0), and Jared's -6
// never found a multicolored card. Pre-existing code already used the equivalent
// `LookupCached(c) ? d->card.IsLand() : c.IsLand()` idiom inline at its graveyard/hand scans, so no
// other deck's behaviour moves. See analysis-FiveColour.md (claude-play sweep, seed 7801 gi1).
inline const Card& ZoneCard(const Card& c)
{
    const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
    return d ? d->card : c;
}

// A/B opt-out (default ON): retain leftover / over-produced mana (depletion + filter
// lands forced to over-tap) into the turn-scoped reserve state.floating_mana, so a later
// same-(main-)phase cast can spend it (CR 500.4). MTG_NO_FLOAT_LEFTOVER=1 disables the
// whole change -> byte-identical to the legacy "waste the leftover" behaviour. Read once.
inline bool FloatLeftoverManaEnabled()
{
    static const bool on = !EnvOn("MTG_NO_FLOAT_LEFTOVER");
    return on;
}

// A/B opt-out (default ON): let the plan enumerator credit a mana rock cast THIS turn
// (Sol Ring -> {C}{C}) toward the rest of the same subset, so lines like
// "Sol Ring -> Ornithopter of Paradise" off one land are enumerated. MTG_NO_ROCK_RAMP=1
// disables it -> byte-identical to the legacy board-only enumeration. Read once.
inline bool RockRampEnumEnabled()
{
    static const bool on = !EnvOn("MTG_NO_ROCK_RAMP");
    return on;
}

// ADOPTED 2026-08-27 -- default ON, opt-out MTG_NO_FLOAT_FEEDS_FILTER=1. Let FLOATING mana feed a
// ramp filter in the accounting pool, as it already does in the payment path.
//
// Ferrous Lake is "{1},{T}: Add {U}{R}" -- it needs a mana INPUT. AddSourceToPool credits its net
// +1 only when HasUntappedRampFeeder finds another untapped SOURCE, and that scan never looks at
// the floating reserve. TapForCostSharedOnce does the opposite: "Pay the {1}: use floating if any,
// else feed one mana from a non-ramp source." So on a board whose only feeder is floating mana, the
// payer can execute the line and the pool says the mana does not exist -- the cast is pruned before
// it is ever enumerated.
//
// Found from a saved viewer artifact (treasure_hunt s3/gi2 T3, verdict legal_not_enumerated):
// floating {R}, Ferrous Lake untapped, Frostboil Snarl tapped, Treasure Hunt {1}{U} in hand. The
// {R} pays the Lake's {1}, the Lake makes {U}{R}, the Hunt casts. None of the 24 enumerated plans
// contained it.
//
// The accounting stays NET, so this cannot double-count: the pool adds the floating {R} once and
// the Lake's +1 net once = 2, which is exactly what the board makes (spend {R}, receive {U}{R}).
// With no floating and no other untapped source the Lake still credits 0, as today.
//
// MEASURED BEFORE ADOPTING, because the precedent said to. This repo had already measured three
// "make the mana projection more accurate" fixes and ALL THREE lost, with zero games better in any
// arm -- the pessimistic projection was acting as a tempo prior
// (docs/design/goblins-enabler-worse-games.md). Those re-ranked two legal lines; this one restores a
// legal line the search could not see at all, which is the distinction that turned out to matter:
//
//   * held-out, 16,000 paired games on 8 fresh seeds disjoint from every tier seed, at the deck's
//     SHIPPED play policy: 4.07619 -> 4.07581 (-0.00038 turns), paired t = -2.450,
//     6 games better / 0 worse / 15,994 tied, 5 seeds better / 3 tied / 0 worse.
//   * regression tier: 5 configs changed, ALL treasure_hunt, every one at an identical average --
//     slower=0 faster=0 play-changed=20. Hinata2 (Izzet Signet, the only other ramp filter in any
//     deck) came back digest-identical, which bounds the blast radius to the Ferrous Lake deck.
//
// Not one game got worse across either measurement. The effect is tiny because the position is rare
// (a ramp filter untapped, NO other untapped source, and floating mana in the pool) -- rare and
// strictly non-negative is the expected profile of a correctness fix, not of a tuning knob.
inline bool FloatFeedsRampFilterEnabled()
{
    static const bool on = !EnvOn("MTG_NO_FLOAT_FEEDS_FILTER");
    return on;
}

// A/B hatch (default OFF): restore the legacy canonical cast order, i.e. provider RANK only, with plan order
// breaking every tie. Set MTG_LEGACY_CAST_TIER_ORDER=1 to disable the cheapest-first ordering of same-tier
// mana accelerants (DecisionProvider::CastCheapestFirstWithinTier) globally -> byte-identical to the
// pre-2026-07-30 engine. Read once by the shared CastOrderLess (ManaPayment.cpp), which both the rollout and
// the executor sort with, so the two can never disagree.
inline bool LegacyCastTierOrder()
{
    static const bool on = EnvOn("MTG_LEGACY_CAST_TIER_ORDER");
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
//
// Protection SCOPE is per-deck (GameState::m_discard_protect, from the profile's
// mulligan.discard_protect; see DiscardPolicy.h for the measured per-deck rationale). Protecting
// EVERY copy regressed Dragonstorm badly, because the highest-MV survivors are then the rituals
// that ARE the deck's mana: overnight s7007 gi79/gi193/gi379 each kept an identical hand but shed
// 4-5 rituals to save payoffs, cast 0-5 spells instead of 6-8, and turned wins on T8/T6/T5 into
// unwon. (gi193's opening hand held TWO Apex of Power; the old code pitched one and won on T6;
// gi79 held one of each but the LIBRARY held more, and pitching it to keep the rituals won on T8.)
// Cleanup discard is NOT a searched decision -- it is this fixed rule, shared by the engine and the
// rollout so they shed in lockstep -- so no amount of depth or budget can route around a bad choice
// here: all three stayed unwon at depth 8 / 20000 ms. Hence the rule itself has to be right.
// Making the discard a real searched decision is the planned successor -- see
// docs/design/searched-cleanup-discard.md.
//
// Counting library copies (LastInDeck) is deck knowledge, not clairvoyance: it is a multiset count
// of what remains, never the ORDER, so it cannot leak which card is drawn next.
inline DiscardProtectScope EffectiveDiscardProtectScope(const GameState& state)
{
    if (const char* ov = DiscardProtectScopeOverride()) { return DiscardProtectScopeFromString(ov); }
    return state.m_discard_protect;
}

// MTG_SHED_STATS (off by default = zero cost): how often the cleanup shed is actually REACHED,
// split by the two callers. This exists because a REAL-PLAY census answers the wrong question: a
// deck whose keep table mulligans away its land-light hands sheds ~never in play, yet the search
// still sheds constantly inside its rollouts (every line that declines the land drop, and every
// keep the MULLIGAN GENERATOR plays out rather than mulligans). Index 0 of the ranking decides
// every one of those with no search above it, so `real == 0` does NOT mean the rule is inert.
// `low_land` counts sheds taken with < 4 lands on the battlefield -- the screwed-and-flooding
// shape, where the ranking picks between cards the player cannot yet cast.
namespace ShedStats
{
    inline bool Enabled() { static const bool v = EnvOn("MTG_SHED_STATS"); return v; }
    inline std::atomic<std::uint64_t> g_real{0};
    inline std::atomic<std::uint64_t> g_rollout{0};
    inline std::atomic<std::uint64_t> g_rollout_lowland{0};
    // How many CLEANUPS shed at least one card, so `rollout / cleanups` is the average number of
    // cards ONE cleanup sheds. It measured 1.35 on the land-light profile, which is what the
    // per-shed re-ranking cost above one consultation -- see CleanupDiscardShedSet, which now
    // decides the whole cleanup in a single call and makes this ratio a shed count, not a
    // recomputation multiplier.
    inline std::atomic<std::uint64_t> g_cleanups{0};
    inline void CountCleanup()
    { if (Enabled()) { g_cleanups.fetch_add(1, std::memory_order_relaxed); } }
    inline void Count(const GameState& state, bool is_rollout)
    {
        if (!Enabled()) { return; }
        if (!is_rollout) { g_real.fetch_add(1, std::memory_order_relaxed); return; }
        g_rollout.fetch_add(1, std::memory_order_relaxed);
        int lands = 0;
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index == state.active_player_index && p.card.IsLand()) { ++lands; }
        }
        if (lands < 4) { g_rollout_lowland.fetch_add(1, std::memory_order_relaxed); }
    }
    struct Dumper {
        ~Dumper()
        {
            if (!Enabled()) { return; }
            std::fprintf(stderr,
                         "\n=== SHED STATS: real=%llu  rollout=%llu (low-land=%llu)"
                         "  cleanups-that-shed=%llu  sheds/cleanup=%.2f ===\n",
                         (unsigned long long)g_real.load(),
                         (unsigned long long)g_rollout.load(),
                         (unsigned long long)g_rollout_lowland.load(),
                         (unsigned long long)g_cleanups.load(),
                         g_cleanups.load() ? (double)g_rollout.load() / (double)g_cleanups.load() : 0.0);
        }
    };
    inline Dumper g_dumper;
}
// ---- Cleanup discard: the full RANKING, not one answer ---------------------------------------
// CR 514.1. The engine used to compute a single index here. It is a ranked LIST now because the
// pick is consumed in two places that both need more than the winner:
//   * AIEngine::ChooseDiscard rolls out every hand card and keeps the win-optimal ones, then
//     TIE-BREAKS on this ranking -- and in a goldfish the tied set is usually everything (shedding
//     any one of eleven lands reaches the same turn inside the horizon), so the ranking, not the
//     rollout, is what actually decides;
//   * the rollout's own cleanup (SimulateEndAndStartNextTurn) has no search at all and takes
//     index 0, so a bad ranking biases every line the search scores.
// Index 0 is exactly the historical single pick, so widening this is byte-identical by itself.
//
// The base ranking is the engine's historical rule, in tiers:
//   A. if DiscardLandsFirst (a land outlet makes lands ammunition): the non-staged LANDS, hand order
//   B. the non-staged, non-protected cards by DESCENDING mana value (ties keep the earlier card)
//   C. last resort, when every non-staged card is protected: non-staged before staged, then max MV
// A provider that knows its deck overrides CleanupDiscardCandidates and orders tier A itself --
// hand order is not a judgement about the cards, and for Treasure Hunt those lands are 13 different
// cards, not eleven copies of "a land". See TreasureHuntProvider.
//
// (A previous MTG_DISCARD_PICK lever bounded how much the ARBITRARY half of this was worth -- see
// docs/design/cleanup-discard-measured.md. Its arms are gone; what it could not measure is what a
// deck-aware ranking is worth, which is what the provider overrides are for.)
inline bool CleanupDiscardProtected(const GameState& state, const Card& c,
                                    const std::vector<std::string>* required_pieces)
{
    if (required_pieces == nullptr) { return false; }
    bool is_req = false;
    for (const std::string& piece : *required_pieces)
    { if (c.m_name == piece) { is_req = true; break; } }
    if (!is_req) { return false; }
    // A RETRACE required piece is NOT lost to a discard -- it stays castable from the graveyard,
    // and this helper only guards FORCED discards (cleanup shed / pitch costs), where the land shed
    // in its place would have paid the retrace cost anyway (weak dominance; see the band-1 rule in
    // TreasureHuntProvider::CleanupDiscardFullRanking). This protection was silently overriding
    // that rule: the provider ranked Throes of Chaos first and the protection dropped it from the
    // preference tier, keeping it in hand and shedding a land -- measured a full turn slower
    // (th s3003 gi=24: T4 vs T3, found by the searched pass trialling past the protection).
    // MTG_PROTECT_RETRACE=1 restores the old blanket protection (the A/B hatch).
    {
        static const bool s_protect_retrace = EnvOn("MTG_PROTECT_RETRACE");
        const CardDefinition* rdef = CardDatabase::Instance().LookupCached(c);
        if (!s_protect_retrace && rdef != nullptr && rdef->params.retrace) { return false; }
    }
    // A redundant required piece stays discardable (and, being high-MV, is picked ahead of the
    // rituals). Count copies in hand -- staged included, since a staged copy is already committed to
    // a line, which is exactly what makes the loose one spare -- plus, under the `deck` scope, the
    // copies still in the library. Protection re-engages once the count hits 1.
    const Player& ap = state.players[state.active_player_index];
    const DiscardProtectScope scope = EffectiveDiscardProtectScope(state);
    if (scope == DiscardProtectScope::All) { return true; }
    // Redundancy is counted over the piece's INTERCHANGEABLE GROUP, not just its own name. Some
    // required pieces are different cards filling ONE role -- Anti-Lifegain's Tainted Remedy and
    // Plague Drone are both "opponent lifegain becomes loss", and you only need one at a time
    // (USER 2026-08-07). Counting by name alone protected BOTH as "last copy", which vetoed the
    // provider's own bucket decision to shed the redundant enabler and made it pitch a payoff
    // instead (antilife s3003 gi226: shed Skyshroud Cutter = 5, shedding the spare enabler or a
    // lesser payoff = 4). Groups are deck knowledge, so the archetype provider supplies them;
    // the default is empty, i.e. name-only counting exactly as before.
    const std::vector<std::string>* group = ResolveProvider(state).InterchangeableRequiredGroup(c.m_name);
    auto same_role = [&](const std::string& other)
    {
        if (other == c.m_name) { return true; }
        if (group == nullptr)  { return false; }
        return std::find(group->begin(), group->end(), other) != group->end();
    };
    int copies = 0;
    for (const Card& h : ap.hand) { if (same_role(h.m_name.str())) { ++copies; } }
    if (scope == DiscardProtectScope::LastInDeck)
    { for (const Card& l : ap.library) { if (same_role(l.m_name.str())) { ++copies; } } }
    return copies <= 1;
}

inline int CleanupDiscardManaValue(const Card& c)
{
    const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
    return def != nullptr ? def->card.m_mana_cost.ManaValue() : c.m_mana_cost.ManaValue();
}

inline bool CleanupDiscardIsLand(const Card& c)
{
    const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
    return def != nullptr ? def->card.IsLand() : c.IsLand();
}

// MTG_DISCARD_ORDER (value-carrying, parsed once): the discard-analysis stage's TESTING-ONLY
// shed-order lever -- "Name;Name;..." trialled as a tier-A order per A/B arm without code.
// nullptr when unset/empty (the shipped path). Shipped orders are provider overrides.
inline const std::vector<std::string>* DiscardOrderTestLever()
{
    static const std::vector<std::string> s_order = []
    {
        std::vector<std::string> out;
        const char* e = std::getenv("MTG_DISCARD_ORDER");
        if (e == nullptr || *e == '\0') { return out; }
        std::string cur;
        for (const char* p = e;; ++p)
        {
            if (*p == ';' || *p == '\0')
            {
                if (!cur.empty()) { out.push_back(cur); cur.clear(); }
                if (*p == '\0') { break; }
            }
            else { cur += *p; }
        }
        return out;
    }();
    return s_order.empty() ? nullptr : &s_order;
}

// The base ranking. `preferred`, when non-empty, is the provider's OWN order for the cards it has an
// opinion about -- it is placed first, and everything it did not mention falls through to tiers B
// and C below in the historical order. That is the whole extension point an archetype needs: a
// deck-aware provider names the cards it wants shed (and the order), and by SAYING NOTHING about a
// card it pushes that card behind every card it did name. "Keep this unless forced" needs no
// separate mechanism -- it is just omission.
inline std::vector<int> CleanupDiscardRankingWithOrder(
    const GameState& state, const std::vector<std::string>* required_pieces,
    const std::vector<int>& preferred)
{
    const Player& ap = state.players[state.active_player_index];
    std::vector<int> out;
    if (ap.hand.empty()) { return out; }
    const int n = static_cast<int>(ap.hand.size());
    // Scratch lives on the STACK, and the two per-card facts the tiers below filter and sort on --
    // mana value (a CardDatabase lookup) and required-piece protection (a name scan, plus a library
    // scan under the `deck` scope) -- are computed ONCE here instead of being re-derived inside a
    // sort comparator. This builder runs once per cleanup shed, which inside a search is ~70k times
    // per 120 land-light games, and it was allocating four heap vectors (plus stable_sort's own temp
    // buffer) and doing O(h log h) database lookups to rank an eight-card hand. Byte-identical: same
    // tiers, same order, same tie-breaks. kScratchMax covers every hand the engine reaches in
    // practice (Treasure Hunt's 15-25-card cleanups included); above it the heap fallback keeps the
    // old shape rather than capping anything.
    constexpr int kScratchMax = 64;
    char taken_buf[kScratchMax];
    int  tier_buf[kScratchMax];
    int  mv_buf[kScratchMax];
    char prot_buf[kScratchMax];
    std::vector<char> taken_heap;
    std::vector<int>  tier_heap;
    std::vector<int>  mv_heap;
    std::vector<char> prot_heap;
    char* taken = taken_buf;
    int*  tier  = tier_buf;
    int*  mv    = mv_buf;
    char* prot  = prot_buf;
    if (n > kScratchMax)
    {
        taken_heap.resize(static_cast<std::size_t>(n));
        tier_heap.resize(static_cast<std::size_t>(n));
        mv_heap.resize(static_cast<std::size_t>(n));
        prot_heap.resize(static_cast<std::size_t>(n));
        taken = taken_heap.data(); tier = tier_heap.data();
        mv    = mv_heap.data();    prot = prot_heap.data();
    }
    for (int i = 0; i < n; ++i)
    {
        taken[i] = 0;
        mv[i]    = CleanupDiscardManaValue(ap.hand[i]);
        // Only ever consulted for non-staged cards (every tier checks the staged flag first), so a
        // staged card skips the scan entirely.
        prot[i]  = (!ap.hand[i].m_is_staged
                    && CleanupDiscardProtected(state, ap.hand[i], required_pieces)) ? 1 : 0;
    }
    out.reserve(static_cast<std::size_t>(n));
    auto push = [&](int i)
    { if (i >= 0 && i < n && !taken[i]) { taken[i] = 1; out.push_back(i); } };

    // Tier A -- the provider's own order, else lands-as-ammunition in hand order.
    // A provider order does NOT get to override required-piece protection or the staged exemption:
    // those are correctness invariants (a staged card is in EXILE and cannot be shed at all; a
    // protected piece is the deck's only copy of a combo piece), not preferences, so entries that
    // violate them are dropped here rather than trusted.
    if (!preferred.empty())
    {
        for (int i : preferred)
        {
            if (i < 0 || i >= n || ap.hand[i].m_is_staged) { continue; }
            if (prot[i]) { continue; }
            push(i);
        }
    }
    else if (const std::vector<std::string>* test_order = DiscardOrderTestLever())
    {
        // MTG_DISCARD_ORDER="Name;Name;..." -- TESTING-ONLY lever for the analyzer's
        // discard-analysis stage to trial a candidate shed order WITHOUT code (per-arm env in
        // its outcome A/B). Shipped rules are always PROVIDER-owned (user ruling 2026-08-07):
        // an adopted order becomes a provider CleanupDiscardCandidates override deferring to
        // this ranking via `preferred` (see HinataProvider). Never set outside the stage.
        // Semantics match a provider order: named cards shed first, in order (all hand copies,
        // hand order); omission = keep; staged and protected entries dropped.
        for (const std::string& name : *test_order)
        {
            for (int i = 0; i < n; ++i)
            {
                if (ap.hand[i].m_name != name || ap.hand[i].m_is_staged) { continue; }
                if (prot[i]) { continue; }
                push(i);
            }
        }
    }
    else if (ResolveProvider(state).DiscardLandsFirst(state))
    {
        for (int i = 0; i < n; ++i)
        { if (!ap.hand[i].m_is_staged && CleanupDiscardIsLand(ap.hand[i])) { push(i); } }
    }

    // (A "spare-copy band" tier lived here 2026-08-06/07 -- shed any name with 2+ hand copies
    // before unique cards. REMOVED as an engine rule: the discard-analysis stage scores it as a
    // label-only hypothesis, and it lost to authored per-deck rules on every deck where dups
    // mattered (hinata/antilife orders beat it head-to-head; dragonstorm it actively hurt,
    // +0.063 overnight -- ritual copies are cumulative fuel). If a future deck's labels ever
    // demand it, implement it in THAT deck's provider. See
    // docs/design/per-deck-discard-analysis-phase.md.)

    // Tier B -- eligible cards, descending mana value, ties keeping the earlier card.
    // The tier is collected in ASCENDING index order, so sorting by (MV desc, index asc) is exactly
    // the stable sort by descending MV it replaces -- same sequence, without stable_sort's heap
    // buffer. (Indices are distinct, so the comparator is a strict total order and std::sort's
    // result is unique.)
    int nb = 0;
    for (int i = 0; i < n; ++i)
    {
        if (ap.hand[i].m_is_staged) { continue; }
        if (prot[i]) { continue; }
        tier[nb++] = i;
    }
    std::sort(tier, tier + nb, [&](int a, int b)
    { if (mv[a] != mv[b]) { return mv[a] > mv[b]; } return a < b; });
    for (int k = 0; k < nb; ++k) { push(tier[k]); }

    // Tier C -- last resort: non-staged before staged, then max mana value, stable.
    int nc = 0;
    for (int i = 0; i < n; ++i) { if (!taken[i]) { tier[nc++] = i; } }
    std::sort(tier, tier + nc, [&](int a, int b)
    {
        const bool sa = ap.hand[a].m_is_staged, sb = ap.hand[b].m_is_staged;
        if (sa != sb) { return !sa; }
        if (mv[a] != mv[b]) { return mv[a] > mv[b]; }
        return a < b;
    });
    for (int k = 0; k < nc; ++k) { push(tier[k]); }

    // MTG_TRACE=discard: what the rule chose BETWEEN. `cands` is the width of the decision -- the
    // number an axis over it has to justify -- and the distribution is the case for a deck-aware
    // ranking. It lives HERE, in the shared builder, not in SelectCleanupDiscardIndex: both real
    // callers (AIEngine::ChooseDiscard, the rollout cleanup) take the provider hook directly, so a
    // trace in the single-index helper sees nothing a goldfish run does. Gated on g_real_resolution
    // (every rollout scope clears it) so a searched run reports the discards the GAME actually
    // made, not the millions the search imagined.
    if (TRACE_ON("discard") && g_real_resolution && !g_le_pitch_ranking && !out.empty())
    {
        int lip = 0, lands_in_hand = 0;
        for (const Permanent& perm : state.battlefield)
        { if (perm.controller_index == state.active_player_index && perm.card.IsLand()) { ++lip; } }
        for (const Card& h : ap.hand) { if (CleanupDiscardIsLand(h)) { ++lands_in_hand; } }
        // tower=1 means a no-max-hand-size land was sitting IN HAND while we discarded -- i.e. the
        // hand limit we are paying was avoidable by playing it. That is a LAND-DROP question, not a
        // discard-ranking one, so it is reported here rather than fixed here.
        int tower = 0;
        const int drop_open = ap.lands_played_this_turn < ap.LandDropsAvailable() ? 1 : 0;
        for (const Card& h : ap.hand)
        {
            const CardDefinition* hd = CardDatabase::Instance().LookupCached(h);
            if (hd != nullptr && hd->params.no_max_hand_size && hd->card.IsLand()) { tower = 1; }
        }
        // game_seed leads the line so a BEHAVIOURAL DIFF between two rule arms can pair decisions by
        // game and stop at each game's first divergence. Without it the lines are unattributable:
        // a multi-game run interleaves them, and after the first changed discard the two arms are
        // playing different games, so comparing by position silently diffs unrelated decisions.
        TRACE("discard",
              "g%llu T%d hand=%zu cands=%zu lip=%d landsinhand=%d tower=%d dropopen=%d -> %s",
              static_cast<unsigned long long>(state.game_seed),
              state.turn_number, ap.hand.size(), out.size(), lip, lands_in_hand, tower, drop_open,
              ap.hand[out.front()].m_name.str().c_str());
    }
    return out;
}

inline std::vector<int> CleanupDiscardRanking(const GameState& state,
                                              const std::vector<std::string>* required_pieces)
{
    return CleanupDiscardRankingWithOrder(state, required_pieces, {});
}

// ---- The WHOLE cleanup, PERFORMED in one call --------------------------------------------------
// CR 514.1 sheds one card at a time, and the rollout's cleanup used to model that literally from the
// OUTSIDE: ask the rule, shed a card, rebuild the hand, ask the rule again, read index 0 again. That
// is one full ranking of the whole hand per DISCARDED CARD -- ~70k of them per 120 land-light games,
// 1.35 per cleanup (MTG_SHED_STATS) -- to re-answer a question the rule had already answered in
// full. The ranking IS the whole decision: everything it does not put in the shed prefix is a KEEP.
//
// So the loop lives here, with the rule, and the whole cleanup is one call: this function moves the
// shed cards to the graveyard, erases them from the hand, and returns how many went. There is no
// intermediate hand for a caller to hold, and no index for a caller to keep valid.
//
//   * `pinned_first` is the searched axis (Plan::discard_choice / scripted_discard_choice): the
//     candidate the committed line pinned for the FIRST shed of this cleanup, -1 for the rule's own
//     pick. Clamped, because the plan was enumerated before this turn's draws and casts changed the
//     hand. `consulted` reports whether the rule answered at all, which is what the caller
//     conditions the consume-and-clear on.
//   * `invert` is the headroom bound (MTG_SHED_WORST): take the WORST-ranked card every shed.
//   * `staged_exempt` mirrors the caller's hand-limit counting: a staged card is in EXILE (CR 514.1
//     counts the HAND), so the real cleanup can never reach one and neither may this.
//
// One consultation answers the whole cleanup only when the rule is PREFIX-STABLE, which it declares
// (DecisionProvider::CleanupDiscardShedStable). An unstable rule -- Treasure Hunt, whose spare-copy
// band moves the surviving Land's Edge to the back once its duplicate is gone -- is asked again
// after each shed, which is what it needs and no worse than before. MTG_DISCARD_SHED_VERIFY=1
// checks the claim on every cleanup against the historical per-shed loop.
std::vector<std::string> CleanupDiscardShedLoopReference(
    const GameState& state, const std::vector<std::string>* required_pieces,
    int count, int pinned_first, bool invert, bool staged_exempt);

inline bool MaybeReplaceGraveyardWithLibraryShuffle(GameState& state, int controller_index,
                                                    const Card& c);

inline int CleanupDiscardShed(GameState& state, const std::vector<std::string>* required_pieces,
                              int count, int pinned_first, bool invert, bool staged_exempt,
                              bool* consulted)
{
    if (consulted != nullptr) { *consulted = false; }
    if (count <= 0) { return 0; }
    Player& ap = state.players[state.active_player_index];
    const DecisionProvider& prov = ResolveProvider(state);

    static const bool s_verify = EnvOn("MTG_DISCARD_SHED_VERIFY");
    std::vector<std::string> ref;
    if (s_verify)
    {
        ref = CleanupDiscardShedLoopReference(state, required_pieces, count, pinned_first,
                                              invert, staged_exempt);
    }

    // The pin and the headroom bound both index the CANDIDATE set, which is the shed order for every
    // provider except the two that narrow it to one entry to bound the executor's searched fan. So
    // they are resolved against the candidates -- one extra consultation, on paths that are off by
    // default (the bound) or fire for a single pinned shed (the axis), never on the shipped hot
    // path. Where the candidate set IS one entry, `invert` has nothing to invert and the bound is
    // inert, exactly as it was when the loop re-consulted a one-entry list every shed.
    int  pin_idx   = -1;
    bool use_worst = false;
    if (pinned_first > 0 || invert)
    {
        const std::vector<int> cand = prov.CleanupDiscardCandidates(state, required_pieces);
        use_worst = invert && cand.size() > 1;
        if (pinned_first > 0 && !cand.empty())
        { pin_idx = cand[std::min(static_cast<std::size_t>(pinned_first), cand.size() - 1)]; }
    }

    const bool stable = prov.CleanupDiscardShedStable();
    std::vector<int> ranked;
    std::vector<std::string> got;   // MTG_DISCARD_SHED_VERIFY only
    int done = 0;
    for (int k = 0; k < count; ++k)
    {
        if (k == 0 || !stable)
        {
            // The shed ORDER, not the candidate set (see CleanupDiscardShedOrder): reading a
            // three-card cleanup off a one-entry candidate list is impossible, and asking again for
            // each card is precisely the loop being retired.
            ranked = prov.CleanupDiscardShedOrder(state, required_pieces);
            if (ranked.empty()) { break; }
            if (consulted != nullptr) { *consulted = true; }
        }
        if (ranked.empty()) { break; }
        const int n = static_cast<int>(ap.hand.size());
        int v = (k == 0 && pin_idx >= 0) ? pin_idx : (use_worst ? ranked.back() : ranked.front());
        if (v < 0 || v >= n) { break; }
        // The ranking's last-resort tier keeps staged cards, so any selector can still land on one.
        // The caller's hand count proved a non-staged card exists, so substitute the first one
        // rather than shedding something GameEngine::CleanupStep could not reach.
        if (staged_exempt && ap.hand[static_cast<std::size_t>(v)].m_is_staged)
        {
            v = -1;
            for (int j = 0; j < n; ++j)
            { if (!ap.hand[static_cast<std::size_t>(j)].m_is_staged) { v = j; break; } }
            if (v < 0) { break; }
        }
        ShedStats::Count(state, /*is_rollout=*/true);   // MTG_SHED_STATS; off by default
        if (s_verify) { got.push_back(ap.hand[static_cast<std::size_t>(v)].m_name.str()); }
        // Progenitus: shuffled into its owner's library instead of the graveyard (replacement) --
        // lockstep with GameEngine::CleanupStep's identical check.
        if (!MaybeReplaceGraveyardWithLibraryShuffle(state, state.active_player_index,
                                                     ap.hand[static_cast<std::size_t>(v)]))
        {
            ap.graveyard.push_back(ap.hand[static_cast<std::size_t>(v)]);
        }
        ap.hand.erase(ap.hand.begin() + v);
        ++done;
        // Re-map the cached order onto the shrunken hand in place -- drop the shed entry, decrement
        // everything that sat behind it. This is what lets ONE consultation answer a multi-card
        // cleanup instead of asking the rule again about cards it has already ranked.
        if (stable)
        {
            std::size_t w = 0;
            for (std::size_t r = 0; r < ranked.size(); ++r)
            {
                const int i = ranked[r];
                if (i == v) { continue; }
                ranked[w++] = (i > v) ? i - 1 : i;
            }
            ranked.resize(w);
        }
    }

    if (s_verify)
    {
        // Compare by NAME: the loop reference works on its own scratch copy, so its indices mean
        // nothing here. (Reading the graveyard tail instead would miss a Progenitus, which the
        // replacement effect shuffles into the LIBRARY -- that read every FiveColour Progenitus shed
        // as a mismatch until the names were recorded at the shed itself.)
        if (got != ref)
        {
            static std::atomic<std::uint64_t> s_bad{0};
            const std::uint64_t seen = s_bad.fetch_add(1, std::memory_order_relaxed);
            if (seen < 40)
            {
                std::string a, b;
                for (const std::string& s : got) { a += s + " "; }
                for (const std::string& s : ref) { b += s + " "; }
                std::fprintf(stderr, "[shed-verify] MISMATCH t%d n=%d batched=[%s] loop=[%s]\n",
                             state.turn_number, count, a.c_str(), b.c_str());
            }
        }
    }
    return done;
}

// WHICH lands Land's Edge pitches, most expendable FIRST -- the same provider ranking the cleanup
// discard uses. Both consume lands out of the same hand for the same reason ("this card is worth
// less to me than what it buys"), so having the outlet pick by HAND ORDER while the cleanup picks
// by a deck-aware ranking meant the deck's own judgement was applied to the RARER of the two: TH
// sheds ~0.8 cards per game to cleanup but can pitch five or more in a single Land's Edge turn.
// It is also where the ranking's conditional rules finally bite -- deprioritising a Reliquary Tower
// once an outlet is out only matters if something is actually choosing which land to burn.
//
// Returns hand indices, at most `count`. The ranking is filtered to LANDS (Land's Edge can only
// discard a land) and topped up in hand order if the provider named fewer lands than needed --
// the outlet may legally pitch any land, so a short ranking must not reduce the damage.
// ADOPTED. MTG_LE_RANKED_PITCH=0 restores the historical hand-order pitch (byte-identical A/B).
// Worth -0.0001 at d0 / -0.0005 at d3 on top of the rung-9 ranking (320k games/arm, fresh seeds),
// and inert on top of rung 1 -- that pairing is the control, since rung 1 names only SPARE cards
// so ordinary lands fall through it in hand order and the pitch has no order to apply. The effect
// is small for a structural reason worth knowing before optimising here: the outlet burns EVERY
// land in hand ~97.6% of the time, and damage is per land, so the order is unobservable unless
// the pitch is a strict subset. See the rung table in DecisionProviders.cpp.
inline std::vector<int> LandsEdgePitchOrder(const GameState& state,
                                            const std::vector<std::string>* required_pieces,
                                            int count)
{
    std::vector<int> out;
    if (count <= 0) { return out; }
    const Player& ap = state.players[state.active_player_index];
    const int n = static_cast<int>(ap.hand.size());
    auto usable = [&](int i)
    { return i >= 0 && i < n && !ap.hand[i].m_is_staged && CleanupDiscardIsLand(ap.hand[i]); };

    static const bool s_ranked = EnvOn("MTG_LE_RANKED_PITCH", true);   // ADOPTED -- see the header
    if (s_ranked)
    {
        // The pitch asks the provider the same question the CLEANUP discard asks, through the same
        // hook -- so without this guard every pitch also emits a [discard] trace line and the
        // cleanup instrument reads a doubled, tower-heavy distribution that no cleanup ever made.
        // Scoped rather than a state pin: the hook returns before this function does.
        g_le_pitch_ranking = true;
        const std::vector<int> ranked =
            ResolveProvider(state).CleanupDiscardCandidates(state, required_pieces);
        g_le_pitch_ranking = false;
        for (int i : ranked)
        {
            if (static_cast<int>(out.size()) >= count) { break; }
            if (usable(i)) { out.push_back(i); }
        }
    }
    for (int i = 0; i < n && static_cast<int>(out.size()) < count; ++i)
    {
        if (!usable(i)) { continue; }
        if (std::find(out.begin(), out.end(), i) == out.end()) { out.push_back(i); }
    }

    // MTG_TRACE=lepitch: is this decision REAL? A ranked pitch can only change a game when the
    // outlet takes a STRICT SUBSET of the lands in hand -- pitch everything and the set is the
    // same set whatever order names it, so the ranking is unobservable. That distinction is
    // invisible in an avg-win-turn delta (it reads as "no effect", exactly like a rule that had
    // its chance and declined it), and this repo has twice believed such a null. `sub` counts the
    // calls that could differ; `diffset` counts the calls that actually DID pick a different set
    // than hand order would have. Gated on g_real_resolution so a searched run reports the pitches
    // the GAME made, not the millions the rollout imagined.
    if (TRACE_ON("lepitch") && g_real_resolution)
    {
        int avail = 0;
        for (int i = 0; i < n; ++i) { if (usable(i)) { ++avail; } }
        std::vector<int> plain;
        for (int i = 0; i < n && static_cast<int>(plain.size()) < count; ++i)
        { if (usable(i)) { plain.push_back(i); } }
        std::vector<int> a = out, b = plain;
        std::sort(a.begin(), a.end());
        std::sort(b.begin(), b.end());
        // The NAMES matter as much as the counts. Avg win turn cannot resolve an effect this rare,
        // but "which cards did the outlet actually burn" is measurable at any sample size -- so a
        // claim like "we are pitching the cycling lands we needed" gets tested directly rather than
        // inferred from a null. Names are of the cards BURNED, comma-separated.
        std::string burned;
        for (int i : out)
        { burned += (burned.empty() ? "" : ","); burned += ap.hand[i].m_name.str(); }
        TRACE("lepitch", "count=%d avail=%d sub=%d diffset=%d burned=%s", count, avail,
              count < avail ? 1 : 0, a != b ? 1 : 0, burned.c_str());
    }
    return out;
}

inline int SelectCleanupDiscardIndex(const GameState& state,
                                     const std::vector<std::string>* required_pieces)
{
    const std::vector<int> rank = ResolveProvider(state).CleanupDiscardCandidates(state, required_pieces);
    return rank.empty() ? -1 : rank.front();
}

// `created_token_haste` is a BOOL in card data, but tokens carry keywords as a vector
// (etb_created_token_keywords). Translate at the boundary rather than carry two mechanisms
// for one property -- CreateToken already maps the string "Haste" to Keyword::Haste, so this
// keeps ONE place deciding what haste means for a created token. Empty for every card that
// does not set the flag, which is the historical argument -> byte-identical.
inline std::vector<std::string> HasteKeywords(bool haste)
{ return haste ? std::vector<std::string>{"Haste"} : std::vector<std::string>{}; }

// Forward declaration: CreateToken is defined further down but used by FireOnCastTriggers.
inline void CreateToken(GameState&, int, int, int, const std::vector<std::string>&,
                        const std::string& = std::string(),
                        const std::vector<std::string>& = std::vector<std::string>());
// THE TWO ENTER CASCADES. Every site that puts a permanent onto the battlefield calls both, in
// this order, and they split by WHOSE ability fires:
//
//   FireEtbWatchers    -- the OTHER permanents already on the battlefield that watch for an entry
//                         ("Whenever a creature you control enters, ..."): Suture Priest / Wardens,
//                         Dragon Tempest's haste grant, Puresteel Paladin's draw, Lathliss's token,
//                         Scourge of Valkas' ping. Plus one deliberate exception -- the newcomer's
//                         own "as this enters, choose a creature type" (Urza's Incubator), which
//                         lives here so executor and rollout share one decision point.
//   FireOwnEtbTriggers -- the ENTERING permanent's own "When this enters, ..." abilities, read
//                         entirely off its own CardParams: etb_life_floor, etb_damage_any,
//                         etb_self_creates_tokens, the ETB tutors, and the rest.
//
// Both are universal, not tribal -- they were named OnDragonEnters / OnGoblinEnters after the
// first deck that needed each, which was already wrong by the time a dozen archetypes hooked them
// and was actively misleading: an "OnGoblinEnters" missing from the search's noncreature-permanent
// projection read as a Goblins-only gap when it silently cost an Enchantment its ETB token (see
// docs/design/etb-cascade-projection-gap.md).
//
// Forward-declared because FireEtbWatchers is mutually recursive with CreateToken (a Lathliss 5/5
// token entering re-fires the cascade). Defined after CreateToken; called FROM CreateToken so
// every token enter fires the watchers too.
inline void FireEtbWatchers(GameState&, int controller, int entered_index);
inline void FireOwnEtbTriggers(GameState&, int controller, int entered_index,
                           const std::string& chosen_tutor, int etb_kx);
// etb_kx sentinel: "PUT entry with no searched destroy-K axis -- pick heuristically at
// resolution" (full rationale at kEtbKxHeuristic's consumers near HeuristicEtbDestroyK).
constexpr int kEtbKxHeuristic = -2;
extern thread_local int g_scripted_tutor_choice;   // defined below (ScriptedTutor)
inline int PermanentManaYield(const GameState&, const Permanent&, const CardDefinition&);   // defined below
inline void EtbUntapLands(GameState&, int controller, int count);                            // defined below
inline void EtbUntapTapAheadIntoFloat(GameState&, int controller, int count);                // defined below
inline int  EtbUntapLandsCredit(const GameState&, int count);                                // defined below
inline void SpendFloatingTowardCost(ManaPool& reserve, ManaCost& cost);                      // defined below
inline bool SetPermTapped(GameState&, int controller, int source_id, bool tapped);            // defined below
inline ManaCost EffectiveActivationCost(const GameState&, int controller, const Card& source,
                                        const ManaCost& printed);                            // defined below
// Forward declaration: the reusable chosen-colour float (defined in the ritual section below) is
// called by ApplySacForMana (Lotus Bloom), which is defined above that section.
inline void AddChosenColorFloat(GameState& state, const std::string& col, int amt);

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
    // MTG_LIFE_TRACE=1 (diagnosis only): per-event ledger of every opponent gain/flip with source
    // + speculation state. Earned permanence by finding the payment-fallback double-drip (the
    // gi72 off-by-one, fixed 2026-08-21) -- the by-hand life count this flip confuses is exactly
    // what it prints. In a search run the stream floods; the TAIL is the real executor's events
    // (each decision's search precedes its apply).
    {
        static const bool s_life_trace = EnvOn("MTG_LIFE_TRACE");
        if (s_life_trace)
        {
            std::fprintf(stderr, "[life] t%d %s amt=%d opp_life=%d->%d spec=%d src=%s\n",
                         state.turn_number, remedied ? "FLIP" : "gain", amount,
                         state.players[opp].life,
                         state.players[opp].life + (remedied ? -amount : amount),
                         g_tap_speculating ? 1 : 0, source.empty() ? "?" : source.c_str());
        }
    }
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
// True when the drip lands we are about to sweep are still LOAD-BEARING for a cast later this
// turn -- i.e. spending them for 1 damage would put the most expensive still-castable hand card
// out of reach. Only meaningful on a deck that actually plays a second main, where the payloads
// are cast post-combat.
//
// This replaces 04b13b0's approach of MOVING the sweep to the last main. That fixed the same
// problem (antilife gi=454: a pre-combat sweep stole the Grove and the second main could no
// longer pay {R}{G}{W} for Fiery Justice, T4 -> T5) but it created a post-main decision point
// that recorded human games predate -- the replay answered it with the engine default and
// claude_s10_gi9 lost a turn, leaving the reference gate red for the whole arc. Keeping the
// sweep WHERE IT WAS and gating it on need introduces no new decision point, so recordings
// still replay.
//
// Deliberately mana-COUNT based, not a full colour solve: this runs on every turn of every
// rollout, and the cheap slack test already separates "genuinely leftover" from "the second
// main wants this". A colour-exact version belongs with the main-2 reservation work
// (docs/design/main2-aware-mana-choice.md), which is the general form of this same question.
inline bool DripManaWantedLaterThisTurn(const GameState& state, int controller_index)
{
    if (!state.uses_second_main) { return false; }
    int untapped = 0, drips = 0;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller_index || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        const bool produces = d->card.IsLand() || !d->params.produces.empty();
        if (!produces) { continue; }
        // Deliberately counts a summoning-sick dork as available: a CanTapNow-exact count was
        // tried (2026-08-27) and measured WORSE -- seeing one source fewer tipped this gate into
        // deferring the drip sweep for a second-main cast greedy play then never made
        // (antilife regression d0 s2002 gi604/gi652 each lost a turn, +0.002 net). The crude
        // count's optimism is load-bearing, same lesson as the prepay projection fixes.
        ++untapped;
        if (d->params.tap_opponent_lifegain > 0) { ++drips; }
    }
    if (drips <= 0) { return false; }
    int best_mv = 0;
    for (const Card& c : state.players[controller_index].hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d || d->card.IsLand()) { continue; }
        const int mv = d->card.m_mana_cost.ManaValue();
        if (mv <= untapped && mv > best_mv) { best_mv = mv; }
    }
    // Sweeping costs `drips` sources. If what we could still cast needs them, keep them.
    return best_mv > untapped - drips;
}

inline void TapDripLandsIfUseful(GameState& state, int controller_index)
{
    if (!ResolveProvider(state).OpponentLifegainUseful(state, controller_index)) { return; }
    // "Leftover" must mean genuinely leftover -- see DripManaWantedLaterThisTurn.
    if (DripManaWantedLaterThisTurn(state, controller_index)) { return; }
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

inline bool CardHasColorNamed(const Card& c, const std::string& col);   // defined below (tutor helpers)

// Spell//land MDFC (Turntimber Symbiosis // Turntimber, Serpentine Wood). A hand card is
// PLAYABLE AS A LAND when it is a real land OR a nonland front with a synthesized MDFC land
// back face. LandFaceDefOf returns the definition every LAND decision (enters-tapped, produces,
// multi-colour ranking) must read: the card's own def for a real land, the BACK-face def for a
// spell//land. nullptr = not playable as a land. Identity for every existing card.
inline const CardDefinition* LandFaceDefOf(const CardDefinition* def)
{
    if (def == nullptr) { return nullptr; }
    if (def->card.IsLand()) { return def; }
    if (!def->params.mdfc_back_name.empty())
    { return CardDatabase::Instance().Lookup(def->params.mdfc_back_name); }
    return nullptr;
}
inline bool PlayableAsLand(const CardDefinition* def) { return LandFaceDefOf(def) != nullptr; }

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
    // Fallback: a SUBTYPE filter (Dragonstorm's tutor_types=["Dragon"]; also Sliver/Goblin/... tutors).
    // Reached only for names that are not one of the card TYPES above, so every existing type-name tutor
    // (Idyllic=Enchantment, Enlightened=Artifact/Enchantment) returns before here -> byte-identical.
    for (const std::string& sub : card.m_subtypes) { if (sub == type_name) { return true; } }
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
// A flat, read-only view of whichever zone a tutor searches. The library and the sideboard are
// different container types (Library vs std::vector<Card>), and copying either on the rollout hot
// path is not acceptable, so this hands back pointers-to-Card in zone order.
inline std::vector<std::reference_wrapper<const Card>>
TutorZoneView(const Player& ap, const std::vector<Card>* wish_pool)
{
    std::vector<std::reference_wrapper<const Card>> out;
    if (wish_pool != nullptr)
    {
        out.reserve(wish_pool->size());
        for (const Card& c : *wish_pool) { out.emplace_back(c); }
        return out;
    }
    out.reserve(ap.library.size());
    for (const Card& c : ap.library) { out.emplace_back(c); }
    return out;
}

inline std::vector<std::string> TutorCandidates(const GameState& state, int controller_index,
                                                const CardParams& pp)
{
    const Player& ap = state.players[controller_index];
    // THE SEARCH ZONE. A wish looks OUTSIDE THE GAME (Living Wish -> Player::sideboard); every
    // other tutor looks at the library. Only the pool differs -- the type filter, the colour
    // filter, the ranking and the whole searched-index axis are shared, which is the entire reason
    // a wish is modelled as a tutor rather than as its own mechanic.
    const std::vector<Card>* wish_pool = pp.wish_from_sideboard ? &ap.sideboard : nullptr;

    // Unpruned audit (MTG_UNPRUNED): return EVERY legal tutor target (distinct names)
    // so the search branches over all of them, instead of the heuristic-narrowed pick.
    if (DecisionUnpruned(UnprunedGate::Tutor))
    {
        std::vector<std::string>        all;
        std::unordered_set<std::string> seen;
        for (const Card& lc : TutorZoneView(ap, wish_pool))
        {
            const CardDefinition* def = CardDatabase::Instance().LookupCached(lc);
            const Card&           card = def ? def->card : lc;
            bool type_ok = false;
            for (const std::string& t : pp.tutor_types)
            { if (CardMatchesTypeName(card, t)) { type_ok = true; break; } }
            if (!CardHasColorNamed(card, pp.tutor_color)) { type_ok = false; }   // colour filter
            if (type_ok && seen.insert(lc.m_name).second) { all.push_back(lc.m_name); }
        }
        return all;
    }

    // Best enabler / wincon / any matching card available in the search zone.
    std::string enabler_name, wincon_name, any_name;
    for (const Card& lc : TutorZoneView(ap, wish_pool))
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
    static const bool on = !EnvOn("MTG_NO_SEARCH_SHUFFLE");
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
// Mid-game reshuffle policy. DEFAULT: the COMMON-RANDOM-NUMBERS reshuffle
// (Library::ShuffleByKey) -- its order depends only on (game_seed, salt, each card's stable
// m_number), NOT on search_count or the exact multiset -- so two similar games on one seed
// reshuffle CONSISTENTLY: a fetch removes its one card and every other card's draw order is
// unchanged, keeping realized draws aligned across an A/B (only genuine play differences move
// the win turn, not shuffle luck). Only the mid-game post-search reshuffle is affected; the
// opening shuffle is untouched, and only decks that fetch/search (antilife, hinata) change.
//   MTG_LEGACY_SHUFFLE forces the historical search-index-keyed Fisher-Yates -- a
//   byte-identical escape hatch (the repo's "always keep a legacy A/B toggle" bar).
//   MTG_STABLE_SHUFFLE is still honoured (no-op now that stable is the default) for scripts
//   that set it explicitly.
inline bool StableShuffleEnabled()
{
    static const bool legacy = EnvOn("MTG_LEGACY_SHUFFLE");
    return !legacy;
}

inline void ShuffleAfterSearch(GameState& state, int controller_index)
{
    if (!SearchShuffleEnabled()) { return; }
    // Decoupling instrument: the SEARCH evaluation shuffles with shuffle_salt_search, the real
    // executor with shuffle_salt. Equal by default -> byte-identical / lockstep. See g_shuffle_eval.
    const uint64_t salt = g_shuffle_eval ? state.shuffle_salt_search : state.shuffle_salt;
    if (StableShuffleEnabled())
    {
        // Key each reshuffle on search_count (the per-game shuffle ORDINAL, bumped below): the Nth
        // reshuffle re-randomizes vs the (N-1)th, so a Ponder/repeated fetch that shuffles actually
        // digs (a fresh top), while still ALIGNING across two same-seed playthroughs at the same
        // ordinal. The CRN benefit -- removing one card leaves every other card's order unchanged --
        // comes from ShuffleByKey itself, NOT from pinning the seed. (Pinning the ordinal to 0 made
        // every reshuffle the same canonical order, so a re-shuffle was a no-op / same top => dig
        // broken; that penalized dig-reliant decks. See docs/design/stable-shuffle.md.)
        state.players[controller_index].library.ShuffleByKey(
            SaltSeed(SearchShuffleSeed(state.game_seed, state.search_count), salt));
    }
    else
    {
        state.players[controller_index].library.Shuffle(
            SaltSeed(SearchShuffleSeed(state.game_seed, state.search_count), salt));
    }
    ++state.search_count;
}

// ---- Progenitus: "If ~ would be put into a graveyard from anywhere, reveal it and shuffle it
// into its owner's library instead." (CardParams::graveyard_replace_shuffle_library, CR 614 self-
// replacement.) Called at the graveyard-push sites reachable for a hand card in this engine (the
// cleanup discard, executor + rollout -- Progenitus cannot die or be otherwise discarded here).
// Returns true if the replacement fired: the card went into the library (then a shuffle) and the
// caller must NOT push it to the graveyard.
inline bool MaybeReplaceGraveyardWithLibraryShuffle(GameState& state, int controller_index,
                                                    const Card& c)
{
    const CardDefinition* def = CardDatabase::Instance().LookupCached(c);
    if (!def || !def->params.graveyard_replace_shuffle_library) { return false; }
    state.players[controller_index].library.push_back(c);
    ShuffleAfterSearch(state, controller_index);
    return true;
}

// PerformTutor -- body in SpellEffects.cpp (see the header note above).
void PerformTutor(GameState& state, int controller_index, const CardParams& pp,
                         const std::string& target_name = "",
                         const std::string& source_name = "Tutor");

// Dragonstorm (Storm) tutor-TO-BATTLEFIELD. Put up to `max_puts` cards matching pp.tutor_types
// (Dragons) from the controller's library ONTO THE BATTLEFIELD, each routed through the shared
// FireEtbWatchers cascade (Scourge ping / Lathliss token) so a put Dragon is a live body, not inert
// -- the #1 wiring requirement. `max_puts` = the STORM total = state.spells_cast_this_turn, which the
// caller passes AS-IS: the storm counter is ++'d at Dragonstorm's own cast, so it already equals
// (prior spells this turn) + 1 = storm copies + the original = the number of Dragons to fetch. The
// helper caps it at the number of matching library cards.
//
// WHICH/ORDER: `preferred` is an optional searched put-list (Dragon names, put first in order); the
// remainder follows ResolveProvider().TutorCandidates order -- for GenericProvider, every matching
// name in LIBRARY order (a future DragonstormProvider owns the Lathliss-first/Scourge-second RANKING
// + selection; this ENGINE step deliberately encodes no ordering/selection heuristic). Multiplicity
// is honoured (3 Scourges can be put). After the puts, SHUFFLE the library (pp.tutor_shuffle_after)
// exactly like a fetch (ShuffleAfterSearch, deterministic CRN reshuffle) -- KEPT per user.
//
// LOCKSTEP: called IDENTICALLY from EffectHandler (executor) and TurnSolver::apply_one (rollout);
// both read the same pre-put library + spells_cast_this_turn, so they put the same Dragons in the
// same order and reshuffle with the same seed. No-op for any card without tutor_to_battlefield.

// Colour filter for tutors ("search for a GREEN creature card" -- Natural Order). `col` is a
// single letter ("G"); empty = no restriction. Reads the DB card's colour mask (tokens are never
// in the library, so the def is authoritative).
inline bool CardHasColorNamed(const Card& c, const std::string& col)
{
    if (col.empty()) { return true; }
    switch (col[0])
    {
        case 'W': return c.HasColor(Color::White);
        case 'U': return c.HasColor(Color::Blue);
        case 'B': return c.HasColor(Color::Black);
        case 'R': return c.HasColor(Color::Red);
        case 'G': return c.HasColor(Color::Green);
        default:  return true;
    }
}

// True while a tutor-to-battlefield is RESOLVING (PerformTutorToBattlefield's provider calls run
// under it). A provider ranking that pre-discounts the tutor's own additional cost (Stompy's
// Natural Order "minus the sac victim" board adjustment) must SKIP the discount here: the cost is
// already paid, the victim is already off the battlefield, and discounting again double-counts it
// (st993: resolution-state n_cre read 1 instead of 2, the lethality gate flipped to "no lethal",
// and the front became raw-power Worldspine over the actually-lethal Craterhoof).
inline thread_local bool g_tutor_at_resolution = false;

// True while candidates are being enumerated FOR THE SEARCH (variant fan); false on the greedy
// paths (d0 decision + rollout leaves), which bind the provider's first pick with no branch.
// Set/reset by TurnSolver around enumeration (was file-local there; shared 2026-08-26 so a
// provider can rank differently when a search will validate the line vs when greedy commits to
// it blind -- first consumer: Stompy's Hornet-Queen-under-pump promotion, which measured
// +4 games at searched depth and -56 turn-steps at greedy when applied to both).
inline thread_local bool g_search_candidate_enum = true;

// True when THIS RUN plays searched (lookahead depth > 0); set once per game by
// TurnSolver::SetSearchedPlay from AIEngine's constructor. Distinct from
// g_search_candidate_enum, which flips per-callsite: this says "a search exists AT ALL to
// validate a speculative line before the executor commits to it". At depth 0 every ranking
// choice is executed blind, so a promotion that only pays off with follow-through must gate
// here. (Was file-local to TurnSolver; shared 2026-08-26 for the Stompy Queen promotion --
// resolution-time is where an unbound Natural Order binds its fetch, and that same site serves
// both the searched executor, whose rollouts priced the line, and greedy d0, which cannot.)
inline thread_local bool g_searched_play = false;

inline void PerformTutorToBattlefield(GameState& state, int controller, const CardParams& pp,
                                      int max_puts,
                                      const std::vector<std::string>& preferred = {},
                                      const std::string& source_name = {})
{
    if (max_puts <= 0 || pp.tutor_types.empty()) { return; }
    struct ResolveScope
    {
        bool prev;
        ResolveScope() : prev(g_tutor_at_resolution) { g_tutor_at_resolution = true; }
        ~ResolveScope() { g_tutor_at_resolution = prev; }
    } _trs;
    Player& ap = state.players[controller];

    auto matches_types = [&](const Card& c) -> bool {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        const Card& card = d ? d->card : c;
        if (!CardHasColorNamed(card, pp.tutor_color)) { return false; }   // Natural Order: green only
        for (const std::string& t : pp.tutor_types)
        { if (CardMatchesTypeName(card, t)) { return true; } }
        return false;
    };

    // The put-list: the SEARCHED `preferred` if the caller supplied one, else the deck provider's
    // TutorToBattlefieldPutOrder heuristic (DragonstormProvider = Lathliss-first/Scourge-second
    // SELECTION + order; every non-Dragonstorm deck -- and Dragonstorm under MTG_UNPRUNED -- returns
    // {} -> byte-identical). This list is an EXACT ordered multiset (repeats == multiplicity).
    std::vector<std::string> put_pref = preferred;
    // PINNED BY THE PLAN = the choice was already made where the human could see it (the searched
    // `preferred`, or the single-put scripted tutor pin below). Only that may suppress the
    // human-play dialog. The provider's heuristic fill is NOT a pin -- it is the autonomous
    // DEFAULT, and treating it as one is what made the Dragon dialog dead code: the gate below read
    // `put_pref.empty()`, DragonstormProvider::TutorToBattlefieldPutOrder always returns a non-empty
    // Lathliss-first order for Dragonstorm, so the condition was false on every real resolution and
    // the human never once chose which Dragons entered -- on the card the whole deck is built to
    // cast. (The comment on the gate even said "the dialog stays for Dragonstorm (preferred always
    // empty)": true of the PARAMETER, false of the variable it actually tested.)
    const bool pinned_by_plan = !preferred.empty();
    if (put_pref.empty())
    {
        put_pref = ResolveProvider(state).TutorToBattlefieldPutOrder(state, controller, pp, max_puts);
    }
    // SINGLE-target put (Natural Order / Turntimber-class): consume the searched tutor pin
    // (Plan::tutor_choice via ScriptedTutor) exactly as PerformTutor does -- index into the
    // provider's deduped candidate list at the TRUE resolution state, clamped (duplicate, never
    // a whiff). Consumed only by a single-put card so the Dragonstorm multi-put path is untouched.
    bool pinned_single = false;
    if (pp.tutor_to_battlefield_single && put_pref.empty() && g_scripted_tutor_choice >= 0)
    {
        pinned_single = true;
        const int pick = g_scripted_tutor_choice;
        g_scripted_tutor_choice = -1;
        std::vector<std::string> cands = ResolveProvider(state).TutorCandidates(state, controller, pp);
        std::vector<std::string> uniq;
        for (const std::string& c : cands)
        { if (std::find(uniq.begin(), uniq.end(), c) == uniq.end()) { uniq.push_back(c); } }
        if (!uniq.empty())
        { put_pref.push_back(uniq[std::min<std::size_t>(static_cast<std::size_t>(pick), uniq.size() - 1)]); }
    }

    // Human-play override: let the player pick WHICH library Dragons enter (the engine keeps the rule's
    // play ORDER). Fires only on the REAL resolution -- g_play_dragon_chooser is nulled by RevealLogPause
    // for every search/rollout/enumeration scope, so the autonomous + search default is the rule ->
    // byte-identical. See GameLogger.h DragonChooser. `human_override` then SKIPS the TutorCandidates
    // fill pass below, so a human who deselects Dragons puts fewer than max_puts (never topped back up).
    bool human_override = false;
    // Single-target put whose target already rode the chosen PLAN VARIANT (Natural Order /
    // Turntimber human play, preferred non-empty): the pick was made in the main_phase menu, so
    // the multi-pick dialog would DOUBLE-ASK (and its multi-int reply desynced the choices
    // stream -- 5d sweep flag, s9104 gi3) -- so those two PINS suppress it, and nothing else does.
    // Dragonstorm has neither pin: its Dragons are chosen here, at resolution, off the real library.
    if (g_play_dragon_chooser && max_puts > 0 && !pinned_by_plan && !pinned_single)
    {
        // Role rank = the provider's fixed play ORDER (Lathliss, Scourges, Utvara, Karrthus, Kolaghan);
        // the same classification DragonstormProvider::TutorToBattlefieldPutOrder uses. Duplicated here
        // (human-play only, guarded by the chooser) so PerformTutorToBattlefield stays deck-agnostic.
        auto role_rank = [](const CardParams& cp, const std::string& nm) -> int {
            if (cp.etb_other_subtype_creates_tokens)       { return 0; }   // Lathliss (token engine)
            if (cp.dragon_ping_on_enter)                   { return 1; }   // Scourge (pinger)
            if (cp.attack_per_matching_creates_tokens > 0) { return 2; }   // Utvara (attack tokens)
            if (cp.grants_haste) { return nm.find("Karrthus") != std::string::npos ? 3 : 4; } // haste
            return 5;                                                      // other Dragon (defensive)
        };
        // Candidate library Dragon copies, sorted into that play order (role, then name, then library
        // index) -- so the human's chosen indices, taken in ascending order, already ARE in play order.
        std::vector<int> lib_idx;
        for (int i = 0; i < static_cast<int>(ap.library.size()); ++i)
        { if (matches_types(ap.library[i])) { lib_idx.push_back(i); } }
        auto rank_of = [&](int i) -> int {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(ap.library[i]);
            return d ? role_rank(d->params, ap.library[i].m_name.str()) : 5;
        };
        std::stable_sort(lib_idx.begin(), lib_idx.end(), [&](int a, int b) {
            int ra = rank_of(a), rb = rank_of(b);
            if (ra != rb) { return ra < rb; }
            if (ap.library[a].m_name != ap.library[b].m_name)
            { return ap.library[a].m_name.str() < ap.library[b].m_name.str(); }
            return a < b;
        });
        std::vector<Card> candidates;
        candidates.reserve(lib_idx.size());
        for (int i : lib_idx) { candidates.push_back(ap.library[i]); }

        // No Dragons left in library -> nothing to pick; leave the rule (human_override stays false).
        if (!candidates.empty())
        {
            // Default subset = the rule's put_pref mapped to candidate copies (first unused copy per name).
            std::vector<int> heur_subset;
            std::vector<bool> used(candidates.size(), false);
            for (const std::string& nm : put_pref)
            {
                for (int i = 0; i < static_cast<int>(candidates.size()); ++i)
                { if (!used[i] && candidates[i].m_name == nm) { used[i] = true; heur_subset.push_back(i); break; } }
            }
            std::sort(heur_subset.begin(), heur_subset.end());

            const std::string src = source_name.empty() ? std::string("Dragonstorm") : source_name;
            std::vector<int> chosen =
                (*g_play_dragon_chooser)(state, controller, src, candidates, max_puts, heur_subset);
            // Validate: unique in-range indices, ascending (= play order), capped at max_puts.
            std::sort(chosen.begin(), chosen.end());
            chosen.erase(std::unique(chosen.begin(), chosen.end()), chosen.end());
            std::vector<std::string> picked;
            for (int i : chosen)
            {
                if (i >= 0 && i < static_cast<int>(candidates.size())
                    && static_cast<int>(picked.size()) < max_puts)
                { picked.push_back(candidates[i].m_name.str()); }
            }
            put_pref = picked;          // may be empty (human declined every Dragon) -> put nothing
            human_override = true;
        }
    }

    // Remaining library copies per matching name (decremented as we commit puts).
    std::unordered_map<std::string, int> remaining;
    for (const Card& c : ap.library) { if (matches_types(c)) { ++remaining[c.m_name]; } }

    std::vector<std::string> put_names;
    // 1) Honour the provider's EXACT ordered put-list: each entry is one put (multiplicity honoured),
    //    capped by library availability + max_puts. Empty put_pref -> this pass is a no-op.
    for (const std::string& nm : put_pref)
    {
        if (static_cast<int>(put_names.size()) >= max_puts) { break; }
        auto it = remaining.find(nm);
        if (it != remaining.end() && it->second > 0) { put_names.push_back(nm); --it->second; }
    }
    // 2) Fill any remaining slots from the provider's TutorCandidates (library order), expanding each
    //    name to its STILL-AVAILABLE copies. With an empty put_pref (every non-Dragonstorm deck +
    //    Dragonstorm unpruned) this reproduces the pre-provider flat loop exactly -> byte-identical.
    //    SKIPPED when the human made an explicit override selection (else deselected Dragons would be
    //    topped back up, defeating the pick-fewer choice); their picks stand exactly as chosen.
    if (!human_override)
    {
        std::vector<std::string> prov =
            ResolveProvider(state).TutorCandidates(state, controller, pp);
        std::unordered_set<std::string> handled;
        for (const std::string& nm : prov)
        {
            if (static_cast<int>(put_names.size()) >= max_puts) { break; }
            if (!handled.insert(nm).second) { continue; }
            auto it = remaining.find(nm);
            if (it == remaining.end()) { continue; }
            while (it->second > 0 && static_cast<int>(put_names.size()) < max_puts)
            { put_names.push_back(nm); --it->second; }
        }
    }
    if (put_names.empty()) { return; }

    // Put each named Dragon: find+remove the first library copy, enter it (preserving its per-copy
    // number), fire FireEtbWatchers. Re-find per put -- erase shifts library indices, and FireEtbWatchers
    // may append tokens to the battlefield but never touches the library.
    for (const std::string& nm : put_names)
    {
        int idx = -1;
        for (int i = 0; i < static_cast<int>(ap.library.size()); ++i)
        { if (ap.library[i].m_name == nm) { idx = i; break; } }
        if (idx < 0) { continue; }
        Card lc = ap.library[idx];
        ap.library.erase(ap.library.begin() + idx);
        const CardDefinition* d = CardDatabase::Instance().LookupCached(lc);
        Permanent perm;
        perm.card              = d ? d->card : lc;
        perm.card.m_number     = lc.m_number;   // preserve per-copy id (logging / state key)
        perm.controller_index  = controller;
        perm.owner_index       = controller;
        perm.entered_this_turn = true;
        state.battlefield.push_back(perm);
        if (g_play_event_sink)   // nulled by RevealLogPause during search/rollout -> byte-identical
        {
            // Name the SPELL that did the putting. This routine is shared by every
            // tutor-to-battlefield card, so the hardcoded "(Dragonstorm)" misattributed every other
            // one of them: a StompySurprise Natural Order read "Worldspine Wurm -- put onto the
            // battlefield (Dragonstorm)" for a deck that has never contained the card
            // (user-reported). `source_name` is already threaded in by both call sites
            // (EffectHandler + the rollout's apply_one); fall back only when it is absent.
            const std::string put_by = source_name.empty() ? std::string("Dragonstorm") : source_name;
            EmitPlayEvent(state.turn_number, "dragonstorm",
                          "\xF0\x9F\x90\x89 " + lc.m_name.str()
                          + " -- put onto the battlefield (" + put_by + ")");
        }
        // #1 wiring requirement: route the put Dragon through the SAME cascade the hard-cast enter
        // uses (Scourge ping -> opponent life loss; Lathliss 5/5 token; token-first ordering baked in).
        FireEtbWatchers(state, controller, static_cast<int>(state.battlefield.size()) - 1);
        // ...and the generic param-ETB cascade (Craterhoof team pump, Hornet Queen tokens,
        // Muxus-class reveals) so a Natural-Order-put creature's ETB fires exactly like a cast one.
        // No-op for every Dragon (no such params) -> Dragonstorm byte-identical.
        // kEtbKxHeuristic: a put Terastodon's destroy-K is picked by the resolution-time
        // lethality heuristic (no searched axis on this path); inert for every other creature.
        FireOwnEtbTriggers(state, controller, static_cast<int>(state.battlefield.size()) - 1,
                       std::string(), kEtbKxHeuristic);
    }

    // "then shuffle your library" (KEPT per user): deterministic CRN reshuffle like a fetch, so
    // post-Dragonstorm draws come from a shuffled deck. Lockstep (same seed in both worlds).
    if (pp.tutor_shuffle_after) { ShuffleAfterSearch(state, controller); }
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
    // WHICH of your creatures a self-targeting pump picks is provider-owned
    // (OwnPumpTargetCandidates); the base rule is the historical highest-power pick.
    std::vector<int> mine;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != controller_index) { continue; }
        if (!CanAttackFull(p, state.battlefield, controller_index)) { continue; }
        mine.push_back(i);
    }
    if (mine.empty()) { return -1; }
    const std::vector<int> ranked =
        ResolveProvider(state).OwnPumpTargetCandidates(state, controller_index, mine);
    return ranked.empty() ? -1 : ranked.front();
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
    // The WHETHER-to-cast gate stays here, NOT in the provider: it is a castability precondition
    // shared in lockstep by the enumeration gate, the rollout, and the executor, and a provider that
    // disagreed with it would desync the three. Only WHICH creature is provider-owned.
    if (!RemedyActive(state, active)) { return -1; }
    std::vector<int> opp;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index == active || !p.card.IsCreature()) { continue; }
        opp.push_back(i);
    }
    if (opp.empty()) { return -1; }
    const std::vector<int> ranked =
        ResolveProvider(state).LifegainRemovalCandidates(state, active, opp);
    return ranked.empty() ? -1 : ranked.front();
}

// Apply a creature-targeting burn's damage AND its delayed "when that creature dies" death trigger
// (Searing Blood / Searing Blaze) in one place, shared by the executor (EffectHandler) and the search
// rollout (ApplyPlanDirect) so both model the SAME life loss. CR 603.7: each Searing Blood sets its
// OWN delayed trigger, so two of them on one creature deal 3+3 = 6 to the controller when it dies --
// the OLD code fired only the single copy that happened to push the creature to lethal (a 4/4 hit by
// two Bloods lost the first Blood's 3). We instead ACCUMULATE each copy's death_trigger_damage as
// `pending_death_trigger` and fire the whole pile the moment the creature FIRST reaches lethal damage
// (from any source -- a later Bolt that finishes an already-Blooded creature still triggers it). The
// lethal-TRANSITION gate (damage_before < toughness <= damage now) fires exactly once and never double.
// Call AFTER adding this source's damage to target.damage; pass the pre-add damage and this source's
// death_trigger_damage (0 for a plain burn -- it still fires any pending left by a prior Blood).
inline void ApplyBurnToCreature(GameState& state, Permanent& target, int damage_before,
                                int source_death_trigger, int caster_index)
{
    if (source_death_trigger > 0) { target.pending_death_trigger += source_death_trigger; }
    const int tough = target.EffectiveToughness();
    if (damage_before < tough && target.damage >= tough && target.pending_death_trigger > 0)
    {
        const int ctrl = target.controller_index;
        state.players[ctrl].life -= target.pending_death_trigger;
        if (ctrl != caster_index) { state.opponent_lost_life_this_turn = true; }
        target.pending_death_trigger = 0;   // fired -- the transition gate also prevents a re-fire
    }
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
    // WHICH creature is provider-owned (BurnCreatureTargetCandidates); the base rule is the
    // historical killable-first / else-first pick, so this is byte-identical.
    std::vector<int> opp;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index == active || !p.card.IsCreature()) { continue; }
        opp.push_back(i);
    }
    if (opp.empty()) { return -1; }   // no opponent creature -> uncastable
    const std::vector<int> ranked =
        ResolveProvider(state).BurnCreatureTargetCandidates(state, active, damage, opp);
    return ranked.empty() ? -1 : ranked.front();
}

// The damage a creature-targeting burn deals to its creature target (Searing Blood 2; Searing Blaze 1,
// or 3 with landfall). Determines whether an own creature would SURVIVE a self-cast for prowess.
inline int CreatureBurnDamage(const CardDefinition& def, const GameState& state)
{
    if (def.params.landfall_damage > 0 && state.ActivePlayer().lands_played_this_turn > 0)
    { return def.params.landfall_damage; }
    return def.params.damage;
}

// The "prowess line" target: with no opponent creature, a creature-targeting burn (Searing Blood /
// Searing Blaze) is normally dead, but casting it on our OWN creature triggers prowess and can be
// lethal. To keep it a real gain rather than self-mutilation, only self-cast onto an own creature that
// SURVIVES the burn (EffectiveToughness > damage) -- it still attacks, at the cost of a little life
// (Blaze). Returns the survivor with the most toughness headroom (safest), or -1 if none. Shared by
// the executor (AIEngine), the rollout (ApplyPlanDirect), and the enumeration gate so all agree.
inline int FindSurvivingOwnCreature(const GameState& state, int active, int damage)
{
    int best = -1, best_tough = -1;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != active || !p.card.IsCreature()) { continue; }
        if (p.EffectiveToughness() <= damage) { continue; }        // would die -> skip
        if (p.EffectiveToughness() > best_tough) { best_tough = p.EffectiveToughness(); best = i; }
    }
    return best;
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

// ==================== Auras (attach-to-creature enchantments) =========================
// A single, battlefield-aware home for the Bogles/hexproof-auras mechanic. Auras attach to a
// creature by STABLE m_number (Permanent::aura_attached_to) -- copy/realloc-safe unlike the dead
// `attached_to` pointer. Every function here is a plain scan over state.battlefield, so it composes
// cleanly with the deep-copied search state. Inert for decks with no is_aura cards (the scans find
// nothing) -> byte-identical for every other deck.

// True if `creature` has at least one Aura you (its controller) control attached to it.
inline bool CreatureHasAura(const Permanent& creature, const GameState& state)
{
    if (!creature.card.IsCreature()) { return false; }
    for (const Permanent& a : state.battlefield)
    {
        if (a.aura_attached_to != creature.card.m_number) { continue; }
        if (a.controller_index != creature.controller_index) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(a.card);
        if (d && d->params.is_aura) { return true; }
    }
    return false;
}

// Does this LAND have shroud from an Aura attached to it (Trace of Abundance)? CR 702.18a: a
// permanent with shroud can't be the target of spells or abilities -- and unlike hexproof that
// applies to its CONTROLLER's spells too, so this restricts our own plays.
//
// The live consequence in this engine is Aura casting: an Aura spell targets its host as it is cast
// (CR 303.4a), so a land already carrying a shroud-granting aura can take no further auras. It is
// deliberately NOT filtered on controller: shroud is symmetric, and an opponent's aura on our land
// would shroud it just the same.
inline bool LandHasShroud(const Permanent& land, const GameState& state)
{
    if (!land.card.IsLand()) { return false; }
    for (const Permanent& a : state.battlefield)
    {
        if (a.aura_attached_to != land.card.m_number) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(a.card);
        if (d && d->params.is_land_aura && d->params.land_aura_grants_shroud) { return true; }
    }
    return false;
}

// "Modified" (Lion Umbra's restriction): the creature has an Aura you control, Equipment (none in
// this engine), or a +1/+1 counter (CR 701.48). = has-aura OR has-a-plus-one-counter here.
inline bool CreatureIsModified(const Permanent& creature, const GameState& state)
{
    if (CreatureHasAura(creature, state)) { return true; }
    for (const Counter& c : creature.counters)
    { if (c.type == Counter::Type::PlusOnePlusOne && c.count > 0) { return true; } }
    return false;
}

// Board-count units for a scaling aura's per-unit grant (aura_scale_kind); see CardParams.
inline int CountAuraScaleUnits(const std::string& kind, const Permanent& aura,
                               const GameState& state, int controller)
{
    int n = 0;
    if (kind == "enchantments")
    {
        for (const Permanent& p : state.battlefield)
            if (p.controller_index == controller && p.card.HasType(CardType::Enchantment)) { ++n; }
    }
    else if (kind == "other_enchantments")
    {
        // "each OTHER enchantment on the battlefield" (any controller). Count all enchantments and
        // subtract this aura itself (always an enchantment on the battlefield when this is called).
        for (const Permanent& p : state.battlefield)
            if (p.card.HasType(CardType::Enchantment)) { ++n; }
        n -= 1;
        (void)aura;
    }
    else if (kind == "artifacts_enchantments")
    {
        for (const Permanent& p : state.battlefield)
            if (p.controller_index == controller
                && (p.card.HasType(CardType::Artifact) || p.card.HasType(CardType::Enchantment))) { ++n; }
    }
    return n < 0 ? 0 : n;
}

// Total {power, toughness} the enchanted `creature` gets from all Auras you control attached to it
// (flat + dynamic scaling) PLUS its own per-aura self-buff (Kor Spiritdancer +2/+2 per Aura on it).
// Added on top of EffectivePower()/lords/dynamic at every combat site. Toughness is currently inert
// vs the passive opponent but is summed for future life-total fidelity.
inline std::pair<int,int> AuraBonusFor(const Permanent& creature, const GameState& state)
{
    if (!creature.card.IsCreature() && !creature.is_animated) { return {0, 0}; }
    const int num  = creature.card.m_number;
    const int ctrl = creature.controller_index;
    int pw = 0, tb = 0, aura_count = 0;
    for (const Permanent& a : state.battlefield)
    {
        if (a.aura_attached_to != num || a.controller_index != ctrl) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(a.card);
        if (!d || !d->params.is_aura) { continue; }
        ++aura_count;
        pw += d->params.aura_power_bonus;
        tb += d->params.aura_tough_bonus;
        if (!d->params.aura_scale_kind.empty())
        {
            int units = CountAuraScaleUnits(d->params.aura_scale_kind, a, state, ctrl);
            pw += d->params.aura_scale_power * units;
            tb += d->params.aura_scale_tough * units;
        }
    }
    const CardDefinition* cd = CardDatabase::Instance().LookupCached(creature.card);
    if (cd && aura_count > 0)
    {
        pw += cd->params.aura_self_buff_power * aura_count;
        tb += cd->params.aura_self_buff_tough * aura_count;
    }
    return {pw, tb};
}

// Equipment analogue of AuraBonusFor: total {power, toughness} granted to `creature` by every
// attached Equipment (Permanent::equipped_to == its card number, same controller). Kept in
// lockstep with AuraBonusFor's call sites -- Combat.cpp's real damage, TurnSolver's two attack
// projections, and the SBA toughness re-check. Equipment stacks (Bonesplitter + Hammer on one
// host both count), matching CR 613: each grants independently.
inline std::pair<int,int> EquipBonusFor(const Permanent& creature, const GameState& state)
{
    if (!creature.card.IsCreature() && !creature.is_animated) { return {0, 0}; }
    const int num  = creature.card.m_number;
    const int ctrl = creature.controller_index;
    int pw = 0, tb = 0;
    for (const Permanent& e : state.battlefield)
    {
        if (e.equipped_to != num || e.controller_index != ctrl) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.card);
        if (!d || !d->params.is_equipment) { continue; }
        pw += d->params.equip_power_bonus;
        tb += d->params.equip_tough_bonus;
    }
    return {pw, tb};
}

// Number of Equipment attached to creature `creature_num` under `controller`. Kor Duelist's ds
// gate (>= 1), Balan's (>= double_strike_min_equipment), Kemba's upkeep token count.
inline int CountEquipmentAttachedTo(const GameState& state, int controller, int creature_num)
{
    int n = 0;
    for (const Permanent& e : state.battlefield)
    {
        if (e.equipped_to != creature_num || e.controller_index != controller) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.card);
        if (d && d->params.is_equipment) { ++n; }
    }
    return n;
}

// Equipment-conditional double strike (creature-side flags): Kor Duelist ("as long as this
// creature is equipped") and Balan ("as long as two or more Equipment are attached"). The third
// ds source beside the own keyword and the lord grant -- OR'd into the same three ds sites
// (Combat.cpp ResolveCombatDamage; TurnSolver attacking-mana-source scorer + PendingAttackDamage)
// so executor and rollout agree.
inline bool HasDoubleStrikeFromEquipment(const Permanent& creature, const GameState& state)
{
    const CardDefinition* cd = CardDatabase::Instance().LookupCached(creature.card);
    if (!cd) { return false; }
    if (!cd->params.double_strike_while_equipped
        && cd->params.double_strike_min_equipment <= 0) { return false; }
    const int n = CountEquipmentAttachedTo(state, creature.controller_index,
                                           creature.card.m_number);
    if (cd->params.double_strike_while_equipped && n >= 1) { return true; }
    return cd->params.double_strike_min_equipment > 0
        && n >= cd->params.double_strike_min_equipment;
}

// The battlefield index of a charge-trigger Equipment (Umezawa's Jitte) attached to `attacker`,
// or -1. Legend rule caps the board at one Jitte, so first match suffices.
inline int FindAttachedChargeEquip(const GameState& state, const Permanent& attacker)
{
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& e = state.battlefield[i];
        if (e.equipped_to != attacker.card.m_number
            || e.controller_index != attacker.controller_index) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.card);
        if (d && d->params.is_equipment && d->params.equip_combat_damage_charges > 0) { return i; }
    }
    return -1;
}

// Umezawa's Jitte combat math, shared by the executor/rollout combat core (which applies the
// counter mutation) and the two attack projections (which only read the damage) so the three
// sites cannot diverge. Timing-faithful closed form for the collapsed combat model:
//   non-DS: one damage event.  spend s0 counters pre-damage -> damage = P + pump*s0; the event
//           dealing > 0 earns +2 counters (trigger approved-collapsed to "damage to a player").
//   DS:     two events. Pre-strike spend pumps BOTH halves ("until end of turn"); the first-
//           strike event earns +2 counters that are mid-step-spendable on the REGULAR half only
//           (the Kor Duelist / Balan interaction); the regular event earns +2 more.
// `spend_request`: -1 = greedy spend-all including DS mid-step earnings (the provider default,
// per-turn damage-optimal); >= 0 = spend exactly that many pre-strike (human choice).
// Returns {total combat damage, counters remaining on the Jitte}.
inline std::pair<int,int> JitteDamageMath(int base_pw, bool ds, int counters, int pump,
                                          int spend_request)
{
    if (ds)
    {
        const int s0 = (spend_request < 0) ? counters : std::min(spend_request, counters);
        int c = counters - s0;
        const int first = base_pw + pump * s0;
        int total = first;
        if (first > 0) { c += 2; }
        const int s1 = (spend_request < 0) ? c : 0;   // mid-step: greedy only
        c -= s1;
        const int second = base_pw + pump * s0 + pump * s1;
        total += second;
        if (second > 0) { c += 2; }
        return { total, c };
    }
    const int s0 = (spend_request < 0) ? counters : std::min(spend_request, counters);
    const int dmg = base_pw + pump * s0;
    int c = counters - s0;
    if (dmg > 0) { c += 2; }
    return { dmg, c };
}

// True if `creature` has shroud -- today only from an attached equip_grants_shroud Equipment
// (Lightning Greaves); no card in the pool has the printed keyword. Shroud means "can't be the
// target of spells or abilities" (CR 702.18b), and EQUIP TARGETS (CR 702.6b) -- so a shrouded
// creature is not a legal equip host, Jitte -1/-1 target, or removal target. Balan's attach-all
// and Skyhunter's attach-dig do NOT target and are unaffected (user-confirmed 2026-08-14).
// Returns the m_number of the shroud-granting equipment via `src_out` when non-null (0 = none)
// so the equip enumerator can offer the move-the-Greaves-off dance.
inline bool CreatureHasShroud(const Permanent& creature, const GameState& state,
                              int* src_out = nullptr)
{
    if (src_out) { *src_out = 0; }
    for (const Permanent& a : state.battlefield)
    {
        if (a.controller_index != creature.controller_index) { continue; }
        if (a.equipped_to != creature.card.m_number) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(a.card);
        if (d && d->params.is_equipment && d->params.equip_grants_shroud)
        {
            if (src_out) { *src_out = a.card.m_number; }
            return true;
        }
    }
    return false;
}

// True if `creature` deals combat damage with lifelink -- its own keyword, any attached
// aura_grants_lifelink Aura, or any attached equip_grants_lifelink Equipment (Loxodon
// Warhammer / Shadowspear). Combat sites gain the controller that much life.
inline bool CreatureHasLifelink(const Permanent& creature, const GameState& state)
{
    if (creature.card.HasKeyword(Keyword::Lifelink)) { return true; }
    for (const Permanent& a : state.battlefield)
    {
        if (a.controller_index != creature.controller_index) { continue; }
        if (a.aura_attached_to == creature.card.m_number)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(a.card);
            if (d && d->params.is_aura && d->params.aura_grants_lifelink) { return true; }
        }
        if (a.equipped_to == creature.card.m_number)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(a.card);
            if (d && d->params.is_equipment && d->params.equip_grants_lifelink) { return true; }
        }
    }
    return false;
}

// The m_numbers of creatures `controller` controls that an Aura with these params may legally
// enchant (CR 601.2c). Empty => the aura is uncastable (no legal target) and stays in hand.
inline std::vector<int> LegalEnchantTargets(const GameState& state, int controller,
                                            const CardParams& pp)
{
    std::vector<int> out;
    // "Enchant land" (Wild Growth / Fertile Ground / Overgrowth / Trace of Abundance): the host is a
    // LAND, not a creature. Which land is a REAL decision -- the bonus rides that land's tap, so
    // enchanting an untapped multi-colour land is not the same play as enchanting a tapped one --
    // so every legal host is emitted as its own plan variant and the search picks (core invariant).
    if (pp.is_land_aura)
    {
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != controller || !p.card.IsLand()) { continue; }
            // An Aura spell TARGETS its host (CR 303.4a), so a shrouded land is not a legal one.
            if (LandHasShroud(p, state)) { continue; }
            out.push_back(p.card.m_number);
        }
        return out;
    }
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller || !p.card.IsCreature()) { continue; }
        if (pp.aura_enchant_requires == "another_aura" && !CreatureHasAura(p, state)) { continue; }
        if (pp.aura_enchant_requires == "modified"     && !CreatureIsModified(p, state)) { continue; }
        out.push_back(p.card.m_number);
    }
    return out;
}

// Resolve the creature this aura should attach to. Prefer the searched `enchant_target` when it names
// a creature `controller` controls; otherwise a deterministic heuristic fallback (a self-buff creature
// like Kor first, then the one already carrying the most auras, then lowest m_number) so the executor
// and rollout agree even if a target was ever left unset. Returns 0 if no creature exists.
//
// `land_aura` switches the host type to LAND ("Enchant land"). Its fallback ranks by the host's
// per-tap yield BEFORE this aura lands (highest first, lowest m_number to break ties): the extra
// mana rides the enchanted land's tap, so doubling up on the land that already makes the most is
// the deterministic default. It is only a fallback -- which land to enchant is emitted as a plan
// variant per legal host, so the search normally decides. Defaults false -> byte-identical.
inline int ResolveEnchantTarget(const GameState& state, int controller, int enchant_target,
                                bool land_aura = false)
{
    if (land_aura)
    {
        // Shroud is checked on BOTH arms: the searched target must still be legal (a board can change
        // between enumeration and resolution -- another aura may have landed on that host in the
        // meantime), and the heuristic fallback must not pick an illegal host either.
        for (const Permanent& p : state.battlefield)
            if (p.controller_index == controller && p.card.IsLand()
                && p.card.m_number == enchant_target && !LandHasShroud(p, state))
            { return enchant_target; }
        int lbest = 0, lbest_yield = -1;
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != controller || !p.card.IsLand()) { continue; }
            if (LandHasShroud(p, state)) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (!d) { continue; }
            const int y = PermanentManaYield(state, p, *d);
            if (y > lbest_yield || (y == lbest_yield && p.card.m_number < lbest))
            { lbest_yield = y; lbest = p.card.m_number; }
        }
        return lbest;
    }
    for (const Permanent& p : state.battlefield)
        if (p.controller_index == controller && p.card.IsCreature()
            && p.card.m_number == enchant_target)
        { return enchant_target; }
    int best = 0, best_score = -1;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller || !p.card.IsCreature()) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        int auras = 0;
        for (const Permanent& a : state.battlefield)
            if (a.aura_attached_to == p.card.m_number && a.controller_index == controller) { ++auras; }
        int score = auras * 4 + ((d && d->params.aura_self_buff_power > 0) ? 1000 : 0);
        if (score > best_score || (score == best_score && p.card.m_number < best))
        { best_score = score; best = p.card.m_number; }
    }
    return best;
}

// FireAttackDigAttach -- body in SpellEffects.cpp. Armored Skyhunter's attack trigger ("look at
// the top six... you may put an Aura or Equipment card... onto the battlefield... attach it to a
// creature you control; rest on the bottom"). Called from GameEngine::CombatPhase AND the
// rollout's combat in lockstep, after attack triggers/pumps and BEFORE ResolveCombatDamage reads
// power -- so a put-and-attached Colossus Hammer counts in THIS combat's damage (the deck's core
// line). The put bypasses the equip cost (attach, not the Equip action) and routes through the
// shared enter cascade (Puresteel's equipment-ETB draw fires). WHICH card and WHICH host are
// provider-owned (AttackDigPutCandidates / AttackDigAttachHost, human-overridable via the dig /
// attach-host choosers). Rest bottomed in examined order (approved deterministic collapse).
void FireAttackDigAttach(GameState& state, int controller, const std::vector<int>& atk_idx);

// ApplyAttachAllEquipment -- body in SpellEffects.cpp. Balan's "{1}{W}: Attach all Equipment you
// control to Balan" (cost paid by the caller). Routes every controlled Equipment not already on
// `balan_id` through the shared ApplyEquip -- Grafted Wargear's re-host sacrifice fires exactly
// as on a normal Equip, and equip costs are bypassed (attach, not the Equip action).
void ApplyAttachAllEquipment(GameState& state, int controller, int balan_id);

// ApplyPutFromHand -- body in SpellEffects.cpp. Stoneforge Mystic's "{1}{W}, {T}: put an
// Equipment card from your hand onto the battlefield" (mana paid by the caller; this taps the
// source). The named card enters UNATTACHED through the shared enter cascade (Puresteel's draw
// fires). Returns false (a no-op) when the source is gone/tapped/sick or the card left hand --
// the stranded-outlet pattern, never a phantom.
bool ApplyPutFromHand(GameState& state, int controller, int source_id,
                      const std::string& put_name);

// ApplyJitteMode -- body in SpellEffects.cpp. Umezawa's Jitte's counter-spend modes: remove one
// charge counter from `jitte_id` for mode 1 = target_id gets -1/-1 until end of turn (the death
// check runs inline against effective toughness, shared by both worlds), mode 2 = controller gains
// charge_lifegain, or mode 3 = the EQUIPPED creature gets +charge_pump_power/+charge_pump_tough
// until end of turn (untargeted; needs a host). Fizzles without spending the counter if the target
// (or, for mode 3, the host) left the battlefield.
void ApplyJitteMode(GameState& state, int controller, int jitte_id, int mode, int target_id);

// PerformLightPawsAttach -- body in SpellEffects.cpp (see the header note above).
void PerformLightPawsAttach(GameState& state, int controller, int cast_aura_mv,
                                   const char* side = "?");

// Fires on-cast triggers from all permanents on the battlefield (e.g. Eidolon of the
// Great Revel; Worthy Knight). Called at cast time from both AIEngine (real game) and
// ApplyPlanDirect (lookahead). Two trigger families:
//   - on_cast_trigger_*: deal damage to the active player when they cast a spell with
//     MV <= on_cast_trigger_max_mv (Eidolon of the Great Revel).
//   - cast_trigger_creates_tokens: create tokens when they cast a spell whose subtypes
//     include cast_trigger_subtype (Worthy Knight: cast a Knight -> 1/1 Human token).
//   - draw_on_aura_cast: Kor Spiritdancer draws a card when you cast an Aura spell.
// Aether Vial deployment is NOT a cast, so it never reaches here -- correct (CR 601.2).
inline void FireOnCastTriggers(GameState& state, const CardDefinition& cast_def)
{
    int mv = cast_def.card.m_mana_cost.ManaValue();
    int active = state.active_player_index;

    // Token specs are collected first: CreateToken push_backs onto the battlefield, which
    // would invalidate a range-for over it. Iterate the original size by index, then create.
    struct TokenSpec { int n, p, t; std::vector<std::string> subs; std::string color;
                       std::vector<std::string> kws; };
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

        // Mana Cannons: casting a multicolored spell deals (its color count) damage to any
        // target -- collapsed to the opponent's face (provably optimal vs the passive
        // opponent; the etb_damage_any precedent). Real damage, like Aria's verse damage.
        // Mana Cannons is not on the battlefield while itself being cast -> never self-fires.
        if (def->params.multicolor_cast_damage_per_color && cast_def.card.IsMulticolored())
        {
            const int before = state.players[1 - active].life;
            const int dmg = cast_def.card.ColorCount();
            state.players[1 - active].life -= dmg;
            state.opponent_lost_life_this_turn = true;
            if (g_play_event_sink)
            {
                EmitPlayEvent(state.turn_number, "damage",
                              "\xF0\x9F\x94\xA5 Mana Cannons (" + cast_def.card.m_name.str() + ", "
                              + std::to_string(dmg) + " colors): " + std::to_string(dmg)
                              + " to opponent (" + std::to_string(before)
                              + "\xE2\x86\x92" + std::to_string(before - dmg) + ")");
            }
        }

        // Ancient Cornucopia: the first spell that's one or more colors cast each turn gains
        // its controller life equal to that spell's color count ("may" always taken -- a free
        // resource with no anti-lifegain tech in-deck). Once each turn per permanent; the
        // used-flag resets at both untap sites (executor + rollout, lockstep).
        if (def->params.colored_cast_lifegain
            && !state.battlefield[i].colored_cast_lifegain_used_this_turn
            && cast_def.card.ColorCount() > 0)
        {
            state.players[active].life += cast_def.card.ColorCount();
            state.players[active].life_gained_this_turn += cast_def.card.ColorCount();
            state.battlefield[i].colored_cast_lifegain_used_this_turn = true;
        }

        // Aria of Flame verse engine: casting an instant or sorcery puts a verse counter on
        // this enchantment, then it deals (verse counters) damage to the opponent. This is
        // real damage (not a lifegain event), so Tainted Remedy does not touch it. Aria is
        // not on the battlefield when it is itself cast, so it never self-triggers.
        if (def->params.verse_damage
            && (cast_def.card.IsInstant() || cast_def.card.IsSorcery()))
        {
            const int before = state.players[1 - active].life;
            state.battlefield[i].verse_counters += 1;
            const int dmg = state.battlefield[i].verse_counters;
            state.players[1 - active].life -= dmg;
            state.opponent_lost_life_this_turn = true;
            // Play-viewer history: Aria's verse damage as a "damage" event (nulled by RevealLogPause
            // during search -> byte-identical). Reports the counter count driving the damage.
            if (g_play_event_sink)
            {
                EmitPlayEvent(state.turn_number, "damage",
                              "\xF0\x9F\x94\xA5 Aria of Flame (" + std::to_string(dmg) + " verse): "
                              + std::to_string(dmg) + " to opponent (" + std::to_string(before)
                              + "\xE2\x86\x92" + std::to_string(before - dmg) + ")");
            }
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
                                     def->params.cast_token_subtypes,
                                     def->params.created_token_color,
                                     HasteKeywords(def->params.created_token_haste)});
            }
        }

        // Young Pyromancer: "Whenever you cast an instant or sorcery spell, create a 1/1 red
        // Elemental creature token." Same collection pass as Worthy Knight above, keyed on the
        // cast spell's TYPE instead of a creature subtype. Aria of Flame (a few lines up) already
        // establishes this exact condition, so the two stay consistent.
        //
        // COPIES DO NOT TRIGGER IT (CR 707.10: a copy put onto the stack by a resolving ability is
        // never "cast"). FireOnCastTriggers runs once per real cast and ResolveSoloTargetTrick's
        // Zada/Mirrorwing fan-out never calls it, so the count is rules-correct without a guard:
        // casting one Fists of Flame into a five-creature Zada fan makes ONE Elemental, not six.
        if (def->params.cast_trigger_instant_sorcery_tokens > 0
            && (cast_def.card.IsInstant() || cast_def.card.IsSorcery()))
        {
            to_create.push_back({def->params.cast_trigger_instant_sorcery_tokens,
                                 def->params.cast_token_power,
                                 def->params.cast_token_toughness,
                                 def->params.cast_token_subtypes,
                                 def->params.created_token_color,
                                 HasteKeywords(def->params.created_token_haste)});
        }

        // Kor Spiritdancer: "Whenever you cast an Aura spell, you may draw a card." Always draw
        // (card advantage is strictly good in a goldfish). Deterministic top-of-library draw,
        // lockstep in both cast paths; the drawn card is a resource for LATER turns (no same-turn
        // re-solve -- conservative, avoids an fd-diverge re-solve divergence; disclosed 6a).
        if (def->params.draw_on_aura_cast && cast_def.params.is_aura)
        {
            Player& kp = state.players[active];
            if (!kp.library.empty())
            {
                std::size_t before = kp.hand.size();
                kp.library.DrawN(1, kp.hand);
                kp.cards_drawn_this_turn += static_cast<int>(kp.hand.size() - before);
                if (g_play_draw_sink)
                {
                    for (std::size_t hi = before; hi < kp.hand.size(); ++hi)
                    { g_play_draw_sink->push_back({ state.turn_number, kp.hand[hi].m_name.str() }); }
                }
            }
        }
    }

    for (const TokenSpec& s : to_create)
    {
        for (int k = 0; k < s.n; ++k)
        { CreateToken(state, active, s.p, s.t, s.subs, s.color, s.kws); }
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
// controlled_lord_idx (optional): pre-filtered battlefield indices of the controller's LordEffect
// permanents. When a caller loops over many creatures against an UNCHANGING battlefield (combat,
// board eval), it can collect this list ONCE instead of having every creature re-scan the whole
// battlefield for the (usually zero) lords -- the per-creature scan was ~3% of a rollout on a
// lord-less deck. Byte-identical: the list is exactly the permanents the in-loop filter keeps
// (controller + LordEffect), and the per-creature logic below (self-exclusion by address, subtype
// matching, scales_per_matching count) is applied unchanged, so no lord is double-counted and an
// "other"-lord still skips itself. nullptr => scan the whole battlefield (original behaviour).
// Is this definition a battlefield LORD (grants a continuous P/T bonus to matching creatures)?
// The LordEffect template, plus the dual-role case: a CREATURE of another template carrying lord
// params (Elvish Archdruid = mana_dork + "Other Elf creatures you control get +1/+1"). Gated on a
// non-zero bonus AND a match scope, so vanilla creatures with subtypes_affected-only params
// (haste granters like Cloudshredder Sliver) do not become P/T lords. Byte-identical for every
// existing deck: the only non-lord-template card with a power_bonus is a sorcery (never a
// battlefield permanent).
inline bool IsLordPermanent(const CardDefinition& def)
{
    if (def.tmpl == CardTemplate::LordEffect) { return true; }
    return def.card.IsCreature()
        && (def.params.power_bonus != 0 || def.params.tough_bonus != 0)
        && (!def.params.subtypes_affected.empty() || def.params.affects_all_creatures);
}

// TAKES THE WHOLE GameState, not just the battlefield (changed 2026-08-23 for Neheb, the Worthy).
// A CONDITIONAL anthem -- "as long as you have one or fewer cards in hand, Minotaurs you control
// get +2/+0" (CardParams::hand_size_anthem_max) -- depends on a player's HAND, which the battlefield
// alone cannot see. The parameter type was changed rather than appended precisely so the compiler
// flags every one of the 17 call sites: an appended `int hand_size` would have bound silently to the
// existing `bool all_creature_types` at the 5-argument sites (bool->int is an implicit conversion),
// producing a wrong bonus with no diagnostic. GameState and std::vector<Permanent> are unrelated
// types, so no call site can compile against the wrong overload. `state.battlefield` is what every
// pre-existing call site already passed, so this is byte-identical for every deck with no
// conditional-anthem card in play.
inline std::pair<int,int> ComputeLordBonus(
    const Card&                    creature,
    const GameState&               state,
    int                            controller_index,
    bool                           all_creature_types = false,
    const Permanent*               self               = nullptr,
    const std::vector<int>*        controlled_lord_idx = nullptr)
{
    const std::vector<Permanent>& battlefield = state.battlefield;
    int pb = 0, tb = 0;

    // Faeburrow Elder characteristic P/T (domain_self_pump): +1/+1 for each color among the
    // controller's permanents -- the creature's OWN buff, not a lord effect, but computed here so
    // every existing combat/eval/SBA call site picks it up. For a hand-card evaluation (the card
    // not yet on the battlefield) this slightly under-counts by the card's own colours -- a
    // conservative projection; exact once it is a permanent (it then counts itself).
    {
        const CardDefinition* sdef = CardDatabase::Instance().LookupCached(creature);
        if (sdef && sdef->params.domain_self_pump)
        {
            bool have[5] = {false, false, false, false, false};
            for (const Permanent& q : battlefield)
            {
                if (q.controller_index != controller_index) { continue; }
                for (int ci = 0; ci < 5; ++ci)
                {
                    if (q.card.HasColor(static_cast<Color>(ci))) { have[ci] = true; }
                }
            }
            const int n = have[0] + have[1] + have[2] + have[3] + have[4];
            pb += n; tb += n;
        }
    }

    // Body for one candidate lord permanent. Returns early (the old `continue`) when the permanent
    // is not a matching lord. Iterated either over the whole battlefield or, when the caller
    // supplied the pre-filtered list, over just the controller's LordEffect permanents -- the checks
    // below still run, so the two paths are byte-identical.
    auto process_lord = [&](const Permanent& lord)
    {
        if (lord.controller_index != controller_index) { return; }
        const CardDefinition* ldef = CardDatabase::Instance().LookupCached(lord.card);
        if (!ldef || !IsLordPermanent(*ldef)) { return; }

        // "Other ..." lords do not buff themselves (CR layer 7c): skip when the lord IS
        // the creature being evaluated. Identity by address within this same battlefield.
        if (ldef->params.lord_excludes_self && self != nullptr && &lord == self) { return; }

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
        if (!matches) { return; }

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
    };
    if (controlled_lord_idx)
    {
        for (int li : *controlled_lord_idx) { process_lord(battlefield[li]); }
    }
    else
    {
        for (const Permanent& lord : battlefield) { process_lord(lord); }
    }

    // CONDITIONAL anthem (Neheb, the Worthy: "As long as you have one or fewer cards in hand,
    // Minotaurs you control get +2/+0"). Deliberately NOT folded into process_lord above: the
    // source is not an IsLordPermanent (its static power_bonus is 0), so it is absent from the
    // caller's pre-filtered controlled_lord_idx list and must be scanned separately. The bonus is
    // SELF-INCLUSIVE ("Minotaurs you control", not "other"), so there is no self-exclusion here.
    // The scan is skipped entirely unless the state actually holds such a permanent, so every
    // deck without one pays a single bool test per call and is byte-identical.
    {
        const std::size_t hand_size = state.players[controller_index].hand.size();
        for (const Permanent& src : battlefield)
        {
            if (src.controller_index != controller_index) { continue; }
            const CardDefinition* sd = CardDatabase::Instance().LookupCached(src.card);
            if (!sd || sd->params.hand_size_anthem_max < 0)                        { continue; }
            if (static_cast<int>(hand_size) > sd->params.hand_size_anthem_max)     { continue; }
            bool matches = sd->params.affects_all_creatures;
            if (!matches && all_creature_types && !sd->params.subtypes_affected.empty())
            { matches = true; }
            if (!matches)
            {
                // Manual subtype loop (not CardHasSubtype): that helper is defined further down
                // this header, and `process_lord` above open-codes the same scan for the same
                // reason.
                for (const std::string& sub : sd->params.subtypes_affected)
                {
                    if (matches) { break; }
                    for (const std::string& cs : creature.m_subtypes)
                    { if (cs == sub) { matches = true; break; } }
                }
            }
            if (!matches) { continue; }
            pb += sd->params.hand_size_anthem_power;
            tb += sd->params.hand_size_anthem_tough;
        }
    }
    return {pb, tb};
}

// Returns true if any lord on the battlefield grants double strike to creature's subtype.
// ds_lord_idx (optional): pre-filtered indices of the controller's double-strike-granting
// permanents (see the ComputeLordBonus note); nullptr => scan the whole battlefield. Byte-identical.
inline bool HasDoubleStrikeFromLords(
    const Card&                    creature,
    const std::vector<Permanent>&  battlefield,
    int                            controller_index,
    bool                           all_creature_types = false,
    const std::vector<int>*        ds_lord_idx        = nullptr)
{
    auto grants = [&](const Permanent& lord) -> bool
    {
        if (lord.controller_index != controller_index) { return false; }
        const CardDefinition* ldef = CardDatabase::Instance().LookupCached(lord.card);
        if (!ldef || !ldef->params.grants_double_strike) { return false; }
        if (all_creature_types && !ldef->params.subtypes_affected.empty()) { return true; }
        for (const std::string& sub : ldef->params.subtypes_affected)
        {
            for (const std::string& cs : creature.m_subtypes)
            {
                if (cs == sub) { return true; }
            }
        }
        return false;
    };
    if (ds_lord_idx)
    {
        for (int li : *ds_lord_idx) { if (grants(battlefield[li])) { return true; } }
    }
    else
    {
        for (const Permanent& lord : battlefield) { if (grants(lord)) { return true; } }
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
// Lightning Greaves: true when an equip_grants_haste Equipment the controller owns is attached
// to this creature (identity = the host's per-instance card.m_number). Sibling of
// HasHasteFromLords, consulted by CanAttackFull only -- see the CardParams::is_equipment note for
// the tap-ability limitation (disclosed).
inline bool HasHasteFromEquip(const Permanent& creature,
                              const std::vector<Permanent>& battlefield,
                              int controller_index)
{
    for (const Permanent& e : battlefield)
    {
        if (e.controller_index != controller_index) { continue; }
        if (e.equipped_to == 0 || e.equipped_to != creature.card.m_number) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.card);
        if (d && d->params.is_equipment && d->params.equip_grants_haste) { return true; }
    }
    return false;
}

// Can this permanent use a {T} ability RIGHT NOW? Battlefield-aware companion to
// Permanent::CanTap(), which is a context-free member and therefore cannot see equipment.
//
// CR 302.6: summoning sickness blocks BOTH attacking and {T} abilities, and haste removes both.
// The engine modelled only half of that for equipment -- HasHasteFromEquip was consulted by
// CanAttackFull alone, so a freshly-cast mana dork wearing Lightning Greaves could attack but
// still could not tap for mana (the limitation disclosed in CardParams::is_equipment). That made
// equip-granted haste a strictly attack-only effect, which is wrong at the rules level and, for a
// deck whose creatures are mostly mana dorks, understates the equipment badly (user-directed,
// 2026-08-08: "for mana dorks and creatures with activated abilities it also means they can be
// activated").
//
// This is the ONE predicate for "may this permanent use a {T} ability now", covering all three
// haste sources (own keyword, lord, equipment). It exists because the engine previously had the
// gap in BOTH directions: the mana-source sites consulted equipment but not lords, while the
// {T}-value sites (Krenko's token tap, Deathrite's graveyard-exile modes) consulted lords but not
// equipment. Nothing distinguishes those two families at the rules level -- CR 302.6 is a single
// restriction lifted by haste from ANY source -- so they now share this predicate. Use it at every
// {T}-ability gate; do NOT re-derive the condition inline.
//
// Note p.CanTap() returns true for a non-creature permanent, which is correct: summoning sickness
// applies only to creatures, so a freshly-played artifact/land may tap immediately.
// MTG_HASTE_TAP_STATS (off by default = zero cost): how often a summoning-sick permanent is
// rescued into using a {T} ability by each GRANTED haste source. Prints one line at exit. This
// exists to keep "no deck pairs these" separate from "the pairing exists and is already handled" --
// conflating them once produced a wrong claim that lord haste was unreachable, when Goblins runs
// Krenko under Chieftain/Warchief.
namespace HasteTapStats
{
    inline bool Enabled() { static const bool v = EnvOn("MTG_HASTE_TAP_STATS"); return v; }
    inline std::atomic<std::uint64_t> g_lords{0};
    inline std::atomic<std::uint64_t> g_equip{0};
    struct Dumper {
        ~Dumper()
        {
            if (!Enabled()) { return; }
            std::fprintf(stderr, "\n=== HASTE-TAP STATS: rescued by lord=%llu  by equipment=%llu ===\n",
                         (unsigned long long)g_lords.load(), (unsigned long long)g_equip.load());
        }
    };
    inline Dumper g_dumper;
}

inline bool CanTapNow(const Permanent& p, const std::vector<Permanent>& battlefield)
{
    if (p.CanTap()) { return true; }
    // "Gains haste until end of turn" (Expedite): haste lifts the summoning-sick {T} restriction
    // too (CR 302.6), so a hasted fresh mana dork may tap for mana the turn it arrives.
    if (p.temp_haste) { return true; }
    if (HasHasteFromLords(p.card, battlefield, p.controller_index, p.is_animated))
    {
        // MTG_HASTE_TAP_STATS: how often a summoning-sick permanent is rescued by each haste
        // source. Distinguishes "this pairing does not exist in any deck" from "it exists and is
        // already handled" -- the two were conflated once (see the Goblins/Krenko note).
        if (HasteTapStats::Enabled()) { HasteTapStats::g_lords.fetch_add(1, std::memory_order_relaxed); }
        return true;
    }
    if (HasHasteFromEquip(p, battlefield, p.controller_index))
    {
        if (HasteTapStats::Enabled()) { HasteTapStats::g_equip.fetch_add(1, std::memory_order_relaxed); }
        return true;
    }
    return false;
}

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
    if (!p.entered_this_turn && !p.gained_control_this_turn) { return true; }
    if (p.card.HasKeyword(Keyword::Haste))      { return true; }
    if (p.temp_haste)                           { return true; }   // Expedite until-EOT haste
    if (HasHasteFromLords(p.card, battlefield, controller_index, p.is_animated)) { return true; }
    return HasHasteFromEquip(p, battlefield, controller_index);
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

// ---- Creature-enter watchers (Creature Giving: Soul/Essence Warden, Suture Priest) ------------
// "Whenever [another] creature [you control / an opponent controls] enters ..." -- fired for EVERY
// creature entering on EITHER side. Called from the universal enter cascade (FireEtbWatchers' top,
// which every enter site already invokes: cast resolution, CreateToken, battlefield puts,
// off-suspend) plus the two opponent-spawn materialisation sites (GameEngine / TurnSolver
// turn-start, lockstep). Gated per-watcher on the params, so a board with no watcher permanents is
// a cheap cached-lookup scan and every other deck is byte-identical.
//
// The "you may" on Suture Priest's triggers is always taken (strictly beneficial). Multiple
// watchers stack (each is its own trigger). The entered permanent itself is excluded ("another");
// a watcher entering alongside others still sees THEIR enters (each enter fires separately).
// DescribeLifeWatchers -- body in SpellEffects.cpp (see the header note above). Formats the one
// aggregated enter/death life-watcher event. COLD by construction: it runs only when a human-play
// event sink is attached, never in a rollout, so keeping it out of this 15-TU header costs nothing.
//
// The watcher lists are keyed by WHICH PLAYER'S LIFE MOVED, not by who controls the watcher -- the
// two differ (Essence Warden's "whenever ANOTHER creature enters" gains for ITS controller no matter
// whose creature entered, while Suture Priest's drain hits the entering creature's controller). So
// `on_subject` = watchers that changed subject_controller's life, `on_other` = the other player's.
// subject_index >= 0 names the creature that ENTERED; -1 = the subject creature DIED.
std::string DescribeLifeWatchers(const GameState& state, int subject_controller, int subject_index,
                                 const std::vector<std::pair<std::string, int>>& on_subject,
                                 const std::vector<std::pair<std::string, int>>& on_other,
                                 int life_before_subject, int life_before_other);

// --- Optional trigger costs ("you may pay {X}. If you do, ...") --------------------------------
// Emiel the Blessed's +1/+1 counter trigger is the only one today. Two payment sources, in order:
//
//  1. `g_etb_optional_payer`, when a caller that OWNS a payment context has installed one. The
//     blink loop does: it is already tapping and paying per iteration, so its payer keeps the
//     trigger's mana on exactly the same books as the activation's. Same thread_local-hook idiom
//     as the g_play_*_chooser family, and nulled the same way -- if nobody installs one, nothing
//     changes.
//  2. Otherwise the TURN-SCOPED FLOAT only.
//
// It deliberately does NOT reach for TapForCostShared on its own. No core resolution effect in
// this engine pays mana by tapping, and adding one here would desynchronise the EXECUTOR's
// `available` accounting (AIEngine carries a pool across a plan's payments; a tap it did not make
// leaves that pool over-counting) -- a silent [fd-diverge] source. The cost of the restriction is
// that a hard-cast creature can miss a counter when the board has untapped mana but no float; that
// under-claims, never over-claims, and is disclosed in Stage 6a.
inline thread_local const std::function<bool(const ManaCost&)>* g_etb_optional_payer = nullptr;

struct EtbOptionalPayerScope
{
    const std::function<bool(const ManaCost&)>* prev;
    explicit EtbOptionalPayerScope(const std::function<bool(const ManaCost&)>* p)
        : prev(g_etb_optional_payer) { g_etb_optional_payer = p; }
    ~EtbOptionalPayerScope() { g_etb_optional_payer = prev; }
};

inline bool PayOptionalTriggerCost(GameState& state, const ManaCost& cost)
{
    if (g_etb_optional_payer != nullptr) { return (*g_etb_optional_payer)(cost); }
    if (!state.floating_mana.CanPay(cost)) { return false; }
    ManaCost remaining = cost;
    SpendFloatingTowardCost(state.floating_mana, remaining);
    return remaining.ManaValue() == 0;
}

inline void FireCreatureEnterWatchers(GameState& state, int entered_controller, int entered_index)
{
    // Play-viewer history (viewer issue #11): this is the DRAIN ENGINE, and it used to move life
    // totals silently -- so a Creature Giving game read as if the engine were inventing damage
    // (issue #10: Suture Priest x2 x (Orchard Spirits + Varchild's Survivors) is exactly the 12->6
    // and 1->-7 that looked invented). Log ONE aggregated event per enter rather than one per
    // watcher, so a two-Priest board reads "Suture Priest x2: opponent -2" instead of two lines.
    // Nothing is collected unless a real human-play sink is attached: g_play_event_sink is nulled
    // by RevealLogPause for every search/rollout/enumeration scope, and this is a per-enter HOT
    // path -- so autonomous play does one predictable null check and is byte-identical.
    // !g_tap_speculating mirrors the lifegain site: never log a phantom that gets backtracked.
    const bool log = (g_play_event_sink != nullptr) && !g_tap_speculating;
    const int life_before_entered = state.players[entered_controller].life;
    const int life_before_other   = state.players[1 - entered_controller].life;
    // Keyed by WHOSE LIFE MOVED, not by who controls the watcher -- Essence Warden gains for ITS
    // controller whichever side the creature entered on, while Suture Priest's drain always hits
    // the entering creature's controller. See DescribeLifeWatchers.
    std::vector<std::pair<std::string, int>> on_entered, on_other;   // watcher name -> trigger count
    auto note = [&](int affected, const std::string& nm)
    {
        auto& v = (affected == entered_controller) ? on_entered : on_other;
        for (auto& e : v) { if (e.first == nm) { ++e.second; return; } }
        v.emplace_back(nm, 1);
    };

    // Entered creature's effective power (base + counters + temp + lords), computed lazily --
    // only a min-power-filtered watcher (Vaultborn Tyrant's "power 4 or greater") reads it.
    int entered_power = -1;
    auto entered_power_now = [&]() -> int
    {
        if (entered_power < 0)
        {
            const Permanent& e = state.battlefield[entered_index];
            entered_power = e.EffectivePower()
                + ComputeLordBonus(e.card, state, e.controller_index,
                                   e.is_animated, &e).first;
        }
        return entered_power;
    };
    // Vaultborn Tyrant's draw rider ("... and draw a card"), shared by the another-creature loop
    // and the self-include tail below.
    auto watcher_draw = [&](int who, int n_draw)
    {
        Player& wp_player = state.players[who];
        for (int k = 0; k < n_draw && !wp_player.library.empty(); ++k)
        {
            std::size_t before = wp_player.hand.size();
            wp_player.library.DrawN(1, wp_player.hand);
            wp_player.cards_drawn_this_turn += static_cast<int>(wp_player.hand.size() - before);
            if (g_play_draw_sink && !g_tap_speculating)
            {
                for (std::size_t hi = before; hi < wp_player.hand.size(); ++hi)
                { g_play_draw_sink->push_back({ state.turn_number, wp_player.hand[hi].m_name.str() }); }
            }
        }
    };

    const int n = static_cast<int>(state.battlefield.size());
    for (int i = 0; i < n; ++i)
    {
        if (i == entered_index) { continue; }   // "another creature"
        const Permanent& w = state.battlefield[i];
        const CardDefinition* wd = CardDatabase::Instance().LookupCached(w.card);
        if (!wd) { continue; }
        const CardParams& wp = wd->params;
        // "Whenever another creature enters, you gain N" (Soul Warden / Essence Warden: ANY side).
        if (wp.any_creature_enters_lifegain > 0)
        {
            state.players[w.controller_index].life += wp.any_creature_enters_lifegain;
            state.players[w.controller_index].life_gained_this_turn += wp.any_creature_enters_lifegain;
            if (log) { note(w.controller_index, w.card.m_name.str()); }
        }
        // "Whenever another creature you control enters, you [may] gain N" (Suture Priest cl. 1;
        // Vaultborn Tyrant adds a minimum-power filter on the ENTERING creature + a draw rider).
        if (wp.own_creature_enters_lifegain > 0 && w.controller_index == entered_controller
            && (wp.creature_enters_min_power <= 0
                || entered_power_now() >= wp.creature_enters_min_power))
        {
            state.players[w.controller_index].life += wp.own_creature_enters_lifegain;
            state.players[w.controller_index].life_gained_this_turn += wp.own_creature_enters_lifegain;
            if (wp.own_creature_enters_draw > 0)
            { watcher_draw(w.controller_index, wp.own_creature_enters_draw); }
            if (log) { note(w.controller_index, w.card.m_name.str()); }
        }
        // "Whenever a creature an opponent controls enters, that player loses N" (Suture Priest
        // clause 2 -- the drain engine; life LOSS, not damage).
        if (wp.opp_creature_enters_life_loss > 0 && w.controller_index != entered_controller)
        {
            state.players[entered_controller].life -= wp.opp_creature_enters_life_loss;
            if (entered_controller != state.active_player_index)
            { state.opponent_lost_life_this_turn = true; }
            if (log) { note(entered_controller, w.card.m_name.str()); }
        }
    }

    // Emiel the Blessed: "Whenever ANOTHER creature you control enters, you may pay {G/W}. If you
    // do, put a +1/+1 counter on it. If it's a Unicorn, put two +1/+1 counters on it instead."
    //
    // This is the deck's SECOND kill and the reason it does not need Shivan Gorge: an Emiel blink
    // loop re-enters its target every iteration, so N iterations put N counters on it. (The target
    // is summoning sick after every blink -- CR 400.7 -- so the huge creature attacks NEXT turn.)
    //
    // The payment is OPTIONAL and taken whenever it is affordable: against a passive opponent a
    // +1/+1 counter is monotone-good and there is nothing else the mana could be held for at this
    // point in the resolution. That is a RESOLUTION heuristic, not a searched branch -- disclosed
    // in Stage 6a. Paid straight out of the turn-scoped float when it is there (the blink loop's
    // tap-ahead leaves it there), else by tapping a source.
    for (std::size_t wi = 0; wi < state.battlefield.size(); ++wi)
    {
        const int watcher_ctrl = state.battlefield[wi].controller_index;
        if (watcher_ctrl != entered_controller) { continue; }
        if (static_cast<int>(wi) == entered_index) { continue; }   // "ANOTHER creature"
        const CardDefinition* wd = CardDatabase::Instance().LookupCached(state.battlefield[wi].card);
        if (!wd || !wd->params.other_creature_etb_counter_cost.has_value()
            || wd->params.other_creature_etb_counters <= 0) { continue; }
        const ManaCost c = wd->params.other_creature_etb_counter_cost.value();
        if (!PayOptionalTriggerCost(state, c)) { continue; }
        int n = wd->params.other_creature_etb_counters;
        const std::string& usub = wd->params.other_creature_etb_counter_subtype;
        if (!usub.empty() && wd->params.other_creature_etb_counters_subtype > 0)
        {
            for (const std::string& st : state.battlefield[entered_index].card.m_subtypes)
            { if (st == usub) { n = wd->params.other_creature_etb_counters_subtype; break; } }
        }
        state.battlefield[entered_index].counters.push_back(
            Counter{ Counter::Type::PlusOnePlusOne, n });
        if (log)
        {
            EmitPlayEvent(state.turn_number, "ability",
                          "\xE2\x9E\x95 " + state.battlefield[wi].card.m_name.str() + ": +"
                          + std::to_string(n) + "/+" + std::to_string(n) + " counter on "
                          + state.battlefield[entered_index].card.m_name.str());
        }
    }

    // Self-inclusive watcher (Vaultborn Tyrant: "Whenever THIS creature or another creature you
    // control with power 4 or greater enters ..."): the entering permanent's OWN params fire for
    // its own enter. Gated on creature_enters_includes_self -> byte-identical elsewhere.
    {
        const Permanent& e = state.battlefield[entered_index];
        const CardDefinition* ed = CardDatabase::Instance().LookupCached(e.card);
        if (ed && ed->params.creature_enters_includes_self
            && ed->params.own_creature_enters_lifegain > 0
            && (ed->params.creature_enters_min_power <= 0
                || entered_power_now() >= ed->params.creature_enters_min_power))
        {
            state.players[e.controller_index].life += ed->params.own_creature_enters_lifegain;
            state.players[e.controller_index].life_gained_this_turn += ed->params.own_creature_enters_lifegain;
            if (ed->params.own_creature_enters_draw > 0)
            { watcher_draw(e.controller_index, ed->params.own_creature_enters_draw); }
            if (log) { note(e.controller_index, e.card.m_name.str()); }
        }
    }

    if (log && (!on_entered.empty() || !on_other.empty()))
    {
        EmitPlayEvent(state.turn_number, "drain",
                      DescribeLifeWatchers(state, entered_controller, entered_index,
                                           on_entered, on_other,
                                           life_before_entered, life_before_other));
    }
}

// Opponent-creature-death watchers (Massacre Wurm clause 2: "whenever a creature an opponent
// controls dies, that player loses 2"). Call AFTER the dead creature has been removed from the
// battlefield, at every site that kills an OPPONENT creature (the -2/-2 sweep below, the
// etb_damage_each_opponent ping prune). Gated on the param -> byte-identical elsewhere.
inline void FireOppCreatureDies(GameState& state, int dead_controller)
{
    // Same silent-life-change class as FireCreatureEnterWatchers above (viewer issue #11): log ONE
    // aggregated event, only when a human-play sink is attached (nulled by RevealLogPause in every
    // search/rollout scope -> autonomous play byte-identical).
    const bool log = (g_play_event_sink != nullptr) && !g_tap_speculating;
    const int life_before = state.players[dead_controller].life;
    std::vector<std::pair<std::string, int>> on_dead;   // watcher name -> trigger count
    for (const Permanent& w : state.battlefield)
    {
        if (w.controller_index == dead_controller) { continue; }
        const CardDefinition* wd = CardDatabase::Instance().LookupCached(w.card);
        if (!wd || wd->params.opp_dies_life_loss <= 0) { continue; }
        state.players[dead_controller].life -= wd->params.opp_dies_life_loss;
        if (dead_controller != state.active_player_index)
        { state.opponent_lost_life_this_turn = true; }
        if (log)
        {
            const std::string nm = w.card.m_name.str();
            bool seen = false;
            for (auto& e : on_dead) { if (e.first == nm) { ++e.second; seen = true; break; } }
            if (!seen) { on_dead.emplace_back(nm, 1); }
        }
    }
    if (log && !on_dead.empty())
    {
        // subject_index = -1: the subject already left the battlefield, so it has no index to name.
        EmitPlayEvent(state.turn_number, "drain",
                      DescribeLifeWatchers(state, dead_controller, -1, on_dead, {},
                                           life_before, state.players[1 - dead_controller].life));
    }
}

// Creates a creature token with the given stats and adds it to the active battlefield.
// The token enters with entered_this_turn = true (subject to summoning sickness unless
// given haste by a lord). Tokens have no card number and an auto-generated name.
inline void CreateToken(
    GameState&                       state,
    int                              controller_index,
    int                              power,
    int                              toughness,
    const std::vector<std::string>&  subtypes,
    const std::string&               color,    // default given at the forward declaration above
    const std::vector<std::string>&  keywords) // ditto
{
    Permanent token;
    // Token colour (StompySurprise: green Insect/Wurm/Elephant tokens are legal "sacrifice a
    // green creature" fodder for Natural Order). Empty = the historical colourless token --
    // byte-identical for every existing call site (nothing else reads token colour).
    if (!color.empty())
    {
        switch (color[0])
        {
            case 'W': token.card.AddColor(Color::White); break;
            case 'U': token.card.AddColor(Color::Blue);  break;
            case 'B': token.card.AddColor(Color::Black); break;
            case 'R': token.card.AddColor(Color::Red);   break;
            case 'G': token.card.AddColor(Color::Green); break;
            default: break;
        }
    }
    std::string token_name = std::to_string(power) + "/" + std::to_string(toughness);
    if (!subtypes.empty()) { token_name += " " + subtypes[0]; }
    token_name            += " Token";
    token.card.m_name      = token_name;   // single intern of the full token name
    token.card.RehashName();
    token.card.AddType(CardType::Creature);
    token.card.m_subtypes  = subtypes;
    // Token keywords. Lathliss's and Utvara's tokens are printed "with flying", which was dropped
    // here because Flying is inert in a goldfish (no blockers) -- until Dragon Tempest made it
    // READABLE ("whenever a creature you control WITH FLYING enters, it gains haste"). Nothing else
    // in the engine reads Keyword::Flying and the TT key folds card.m_name_hash, not the keyword
    // mask, so populating this leaves every existing token-making deck byte-identical.
    // (CardDatabase's KeywordFromString is file-local; this is the token-legal subset -- extend it
    // when a token needs a keyword the engine actually reads.)
    for (const std::string& kw : keywords)
    {
        if      (kw == "Flying")    { token.card.AddKeyword(Keyword::Flying); }
        else if (kw == "Haste")     { token.card.AddKeyword(Keyword::Haste); }
        else if (kw == "Trample")   { token.card.AddKeyword(Keyword::Trample); }
        else if (kw == "Vigilance") { token.card.AddKeyword(Keyword::Vigilance); }
    }
    token.card.m_power     = power;
    token.card.m_toughness = toughness;
    token.card.m_number    = state.next_token_number++;   // unique per-copy id (GameState.h note)
    token.controller_index = controller_index;
    token.owner_index      = controller_index;
    token.entered_this_turn = true;
    token.is_token          = true;   // Lathliss "nontoken Dragon" gate reads this (loop-safe)
    state.battlefield.push_back(token);
    // A token Dragon (Lathliss 5/5, Utvara 6/6) entering also fires the Dragonstorm cascade: it
    // re-pings every Scourge (via FireEtbWatchers step 2) but, being a token, never re-triggers
    // Lathliss (nontoken gate). No-op for every non-Dragon token (early subtype return) -> all
    // existing token-making decks (Adeline, Forbidden Orchard, Sliver Hive) are byte-identical.
    FireEtbWatchers(state, controller_index, static_cast<int>(state.battlefield.size()) - 1);
}

// Token that is a COPY of a real card (Vaultborn Tyrant's "create a token that's a copy of it").
// Copies the card wholesale (name, P/T, subtypes, keywords), so LookupCached resolves the REAL
// definition and the copy's own abilities stay live (its enter fires the watchers; is_token gates
// it out of "not a token" death triggers). Enters through the universal cascade like any token.
inline void CreateTokenCopyOfCard(GameState& state, int controller, const Card& src)
{
    Permanent token;
    token.card              = src;
    token.card.m_def        = nullptr;                       // re-resolve by name (same def)
    token.card.m_number     = state.next_token_number++;     // fresh per-copy id
    token.controller_index  = controller;
    token.owner_index       = controller;
    token.entered_this_turn = true;
    token.is_token          = true;
    state.battlefield.push_back(token);
    FireEtbWatchers(state, controller, static_cast<int>(state.battlefield.size()) - 1);
}

// ---- Dragonstorm kill-engine shared helpers (Scourge / Lathliss / Utvara) -----------
// One cascade + one attack-token maker + one firebreathing routine, called IDENTICALLY from the
// executor (EffectHandler / GameEngine / AIEngine) and the rollout (TurnSolver) so the ETB ping
// chain, token generation, and mana->power conversion stay lockstep (Stage-5 fd-diverge otherwise).

inline bool CardHasSubtype(const Card& c, const std::string& sub)
{
    for (const std::string& s : c.m_subtypes) { if (s == sub) { return true; } }
    return false;
}

// "As this permanent enters, choose a creature type" (Urza's Incubator). Returns the interned id of
// the creature subtype the controller's DECK is built around: the subtype carried by the most
// creature cards across all of that player's zones. That keeps the CARD generic -- an Incubator in a
// Dragons deck chooses Dragon, in a Slivers deck Sliver -- instead of baking one tribe into
// cards.json, which is wrong for every other deck that runs it.
//
// The union of the scanned zones is the player's whole deck, so this is a GAME-CONSTANT: every
// Incubator that ever enters in one game gets the same answer regardless of when it enters. That is
// what makes it safe to decide here (deterministically, in both worlds off the same shared helper)
// rather than as a searched decision. Ties break on the LOWEST interned id, which is assignment
// order in cards.json -- arbitrary, but the tie means the two tribes are equally represented, and a
// deck built around two equally-sized tribes is not a case any shipped deck presents. If a deck ever
// wants a genuine per-copy choice, this becomes a plan sub-decision and Permanent::chosen_subtype_id
// must then be folded into the sim key as a real searched branch (it already is, see BuildSimKey).
inline uint16_t DominantCreatureSubtypeId(const GameState& state, int controller)
{
    // Small open-addressed tally: decks carry a handful of distinct creature subtypes.
    uint16_t ids[32]   = {0};
    int      counts[32] = {0};
    int      n = 0;
    auto tally = [&](const Card& c)
    {
        if (!c.IsCreature()) { return; }
        for (std::size_t i = 0; i < c.m_subtypes.size(); ++i)
        {
            const uint16_t id = c.m_subtypes.IdAt(i);
            if (id == SubtypeRegistry::kNone) { continue; }
            int slot = -1;
            for (int k = 0; k < n; ++k) { if (ids[k] == id) { slot = k; break; } }
            if (slot < 0)
            {
                if (n >= 32) { continue; }        // pathological subtype spread: ignore the tail
                slot = n++; ids[slot] = id; counts[slot] = 0;
            }
            ++counts[slot];
        }
    };
    const Player& p = state.players[controller];
    for (const Card& c : p.library)         { tally(ZoneCard(c)); }
    for (const Card& c : p.hand)            { tally(ZoneCard(c)); }
    for (const Card& c : p.graveyard)       { tally(ZoneCard(c)); }
    for (const StagedCard& s : p.staged_cards)    { tally(ZoneCard(s.card)); }
    for (const SuspendedCard& s : p.suspended_cards) { tally(ZoneCard(s.card)); }
    // Tokens are excluded: they are not deck cards, and counting them would let a board of Lathliss
    // 5/5s re-decide the tribe mid-game (breaking the game-constant property above).
    for (const Permanent& q : state.battlefield)
    { if (q.controller_index == controller && !q.is_token) { tally(q.card); } }

    uint16_t best = SubtypeRegistry::kNone;
    int      best_n = 0;
    for (int k = 0; k < n; ++k)
    {
        if (counts[k] > best_n || (counts[k] == best_n && best != SubtypeRegistry::kNone && ids[k] < best))
        { best = ids[k]; best_n = counts[k]; }
    }
    return best;
}

// The subtype id a reducer permanent actually discounts: its CHOSEN type when the card says "choose
// a creature type" (Urza's Incubator), else its printed reduces_spell_subtype (Goblin Warchief).
inline uint16_t ReducerSubtypeId(const CardDefinition& def, const Permanent& perm)
{
    if (def.params.chooses_creature_type) { return perm.chosen_subtype_id; }
    return SubtypeRegistry::Instance().Id(def.params.reduces_spell_subtype);
}

// Number of Dragons `controller` controls (by printed/token subtype "Dragon"). Counts tokens
// (they carry subtype "Dragon"); animated lands are ignored (never Dragons in these decks).
inline int CountControlledDragons(const GameState& state, int controller)
{
    int n = 0;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index == controller && CardHasSubtype(p.card, "Dragon")) { ++n; }
    }
    return n;
}

// A permanent just entered under `controller` at battlefield slot `entered_index`: fire every
// OTHER permanent's enter-watching trigger. See the pair note at the forward declaration for how
// this splits from FireOwnEtbTriggers, and for why neither is Dragon- or Goblin-specific.
//
// In order: the newcomer's own choose-a-creature-type (the one self-effect that lives here, so the
// executor and the rollout share one decision point), creature-enter watchers (Suture Priest /
// Wardens), Dragon Tempest's haste-on-flying-enter, Puresteel Paladin's equipment draw, then the
// two Dragonstorm-engine steps this function was originally written for -- Lathliss ("another
// NONTOKEN Dragon enters -> 5/5 Dragon token") and Scourge of Valkas ("this or another Dragon
// enters -> deal X = Dragons you control to any target", modelled as opponent life loss).
//
// Deterministic token-first ordering between those two (the unambiguously optimal goldfish line):
// create Lathliss's token FIRST (it re-pings Scourge via the recursive CreateToken at the new,
// higher count), THEN resolve the newcomer's own Scourge pings at that higher count.
//
// Loop-safety: a Scourge ping creates no permanents (can't loop); a Lathliss token is is_token so
// step 1 skips it (never re-triggers Lathliss) though it still re-pings Scourge (correct).
//
// Every block below is param-gated, so a deck using none of these params pays only the scans and
// is byte-identical.
inline void FireEtbWatchers(GameState& state, int controller, int entered_index)
{
    if (entered_index < 0 || entered_index >= static_cast<int>(state.battlefield.size())) { return; }
    // Creature-enter watchers (Creature Giving: Wardens / Suture Priest). This function is the
    // UNIVERSAL enter cascade -- every enter site already calls it -- so the watcher fire lives at
    // its top, BEFORE the Dragon-subtype early-out, gated on the newcomer being a creature. A gift
    // token created for the OPPONENT (CreateToken -> here) therefore drains through Suture Priest
    // exactly like a cast creature. Param-gated scan -> byte-identical for every other deck.
    // "As this permanent enters, choose a creature type" (Urza's Incubator). Decided HERE, at the
    // universal enter cascade, so the executor and the rollout make the identical choice off one
    // shared helper. Above the creature early-out because the Incubator is an artifact.
    // Param-gated -> byte-identical for every card that does not choose a type.
    {
        const Permanent& e = state.battlefield[entered_index];
        const CardDefinition* ed = CardDatabase::Instance().LookupCached(e.card);
        if (ed && ed->params.chooses_creature_type)
        {
            state.battlefield[entered_index].chosen_subtype_id =
                DominantCreatureSubtypeId(state, e.controller_index);
        }
    }
    if (state.battlefield[entered_index].card.IsCreature())
    { FireCreatureEnterWatchers(state, state.battlefield[entered_index].controller_index, entered_index); }
    // Dragon Tempest: "Whenever a creature you control with flying enters, it gains haste until end
    // of turn." Rides this universal cascade so it covers a hard-cast Dragon AND every token
    // (Lathliss's 5/5 and Utvara's 6/6 both enter with flying, and CreateToken routes through here)
    // -- which is the whole point of the card in this deck: a Lathliss token made in the main phase
    // can attack the turn it appears. temp_haste is the Expedite field, read by CanAttackFull /
    // CanTapNow and cleared each cleanup. Param-gated scan -> byte-identical for every other deck.
    {
        Permanent& newcomer = state.battlefield[entered_index];
        if (newcomer.card.IsCreature() && !newcomer.temp_haste
            && newcomer.card.HasKeyword(Keyword::Flying))
        {
            const int nctrl = newcomer.controller_index;
            bool grant = false;
            for (const Permanent& w : state.battlefield)
            {
                if (w.controller_index != nctrl) { continue; }
                const CardDefinition* wd = CardDatabase::Instance().LookupCached(w.card);
                if (wd && wd->params.haste_on_flying_enter) { grant = true; break; }
            }
            if (grant) { state.battlefield[entered_index].temp_haste = true; }
        }
    }
    // Puresteel Paladin: "Whenever an Equipment you control enters, you may draw a card." Lives
    // at this universal cascade (NOT the on-cast hook) so it fires for a cast equipment AND one
    // put onto the battlefield (Stoneforge Mystic's ability, Armored Skyhunter's attack dig) --
    // CR 603.6a, the ETB-vs-cast checkpoint. "May" always taken: a draw is strictly good in a
    // goldfish; the empty-library guard skips (disclosed 6a). Each Paladin triggers separately.
    // Param-gated scan -> byte-identical for every deck without the param.
    {
        const Permanent& entered = state.battlefield[entered_index];
        const CardDefinition* edef = CardDatabase::Instance().LookupCached(entered.card);
        if (edef && edef->params.is_equipment)
        {
            const int ectrl = entered.controller_index;
            int draws = 0;
            for (const Permanent& w : state.battlefield)
            {
                if (w.controller_index != ectrl) { continue; }
                const CardDefinition* wd = CardDatabase::Instance().LookupCached(w.card);
                if (wd && wd->params.draw_on_equipment_etb) { ++draws; }
            }
            if (draws > 0)
            {
                Player& kp = state.players[ectrl];
                for (int k = 0; k < draws && !kp.library.empty(); ++k)
                {
                    std::size_t before = kp.hand.size();
                    kp.library.DrawN(1, kp.hand);
                    kp.cards_drawn_this_turn += static_cast<int>(kp.hand.size() - before);
                    if (g_play_draw_sink)
                    {
                        for (std::size_t hi = before; hi < kp.hand.size(); ++hi)
                        { g_play_draw_sink->push_back({ state.turn_number, kp.hand[hi].m_name.str() }); }
                    }
                }
            }
        }
    }
    // Early out for every NONCREATURE enter. This used to early-out on "not a Dragon", which made
    // the token cascade below Dragon-only; Sethron, Hurloon General needs the same shape for
    // Minotaurs, so the subtype gate moved DOWN to where it belongs -- STEP 1 already filters per
    // SOURCE on that source's own etb_token_requires_subtype, and STEP 2 (the Scourge ping) keeps
    // its explicit Dragon gate. Byte-identical for every existing deck: a non-Dragon creature now
    // runs STEP 1's scan, but no shipped source has etb_other_subtype_creates_tokens with a
    // non-Dragon requirement, so it creates nothing and STEP 2 still returns early.
    if (!state.battlefield[entered_index].card.IsCreature()) { return; }
    const bool entered_is_token = state.battlefield[entered_index].is_token;
    // Copy the newcomer's subtypes: CreateToken below push_backs and may reallocate the vector,
    // which would dangle a reference (entered_index itself stays valid -- tokens only append).
    const Card entered_card = state.battlefield[entered_index].card;

    // STEP 1 -- Lathliss tokens (created first). Each Lathliss makes a 5/5 for the newcomer if it
    // is a NONTOKEN Dragon that is not the Lathliss itself and matches the required subtype.
    if (!entered_is_token)
    {
        struct Spec { int p, t; std::vector<std::string> subs; std::string color;
                      std::vector<std::string> kws; };
        std::vector<Spec> specs;
        const int bf_size = static_cast<int>(state.battlefield.size());
        for (int i = 0; i < bf_size; ++i)
        {
            const Permanent& src = state.battlefield[i];
            if (src.controller_index != controller) { continue; }
            const CardDefinition* sdef = CardDatabase::Instance().LookupCached(src.card);
            if (!sdef || !sdef->params.etb_other_subtype_creates_tokens) { continue; }
            // "another Dragon" (Lathliss) skips the source's own enter; "Sethron OR another
            // nontoken Minotaur" (etb_token_includes_self) does not.
            if (i == entered_index && !sdef->params.etb_token_includes_self) { continue; }
            if (!sdef->params.etb_token_requires_subtype.empty()
                && !CardHasSubtype(entered_card, sdef->params.etb_token_requires_subtype)) { continue; }
            specs.push_back({ sdef->params.etb_created_token_power,
                              sdef->params.etb_created_token_toughness,
                              sdef->params.etb_created_token_subtypes,
                              sdef->params.created_token_color,
                              sdef->params.etb_created_token_keywords });
        }
        for (const Spec& s : specs)
        {
            CreateToken(state, controller, s.p, s.t, s.subs, s.color, s.kws);   // re-pings Scourge
        }
    }

    // STEP 2 -- Scourge pings for the newcomer at the CURRENT Dragon count (now includes any
    // token created in step 1). Life loss to the opponent's face, the same sink combat / direct
    // damage use for the win projection -> a Dragonstorm/hard-cast wave shows up as lethal in the
    // rollout (opp.life <= 0). Multiple Scourges each ping.
    const int dragon_count = CountControlledDragons(state, controller);
    if (dragon_count <= 0) { return; }
    const int opp = 1 - controller;
    const int bf_now = static_cast<int>(state.battlefield.size());
    for (int i = 0; i < bf_now; ++i)
    {
        const Permanent& src = state.battlefield[i];
        if (src.controller_index != controller) { continue; }
        const CardDefinition* sdef = CardDatabase::Instance().LookupCached(src.card);
        if (!sdef || !sdef->params.dragon_ping_on_enter) { continue; }
        const int before = state.players[opp].life;
        state.players[opp].life -= dragon_count;
        state.opponent_lost_life_this_turn = true;
        if (g_play_event_sink)   // nulled by RevealLogPause during search/rollout -> byte-identical
        {
            EmitPlayEvent(state.turn_number, "damage",
                          "\xF0\x9F\x90\x89 " + src.card.m_name.str() + " ("
                          + std::to_string(dragon_count) + " Dragons): "
                          + std::to_string(dragon_count) + " to opponent ("
                          + std::to_string(before) + "\xE2\x86\x92"
                          + std::to_string(before - dragon_count) + ")");
        }
    }
}

// ---- Goblins tribal ETB cascade (self-tokens / ETB burn / Matron tutor / Muxus reveal) --------
// One shared hook, called IDENTICALLY from the executor (EffectHandler::EnterBattlefield) and the
// rollout (TurnSolver creature-enter) so the ETB effects stay lockstep (fd-diverge otherwise).
// Every branch is gated on a param, so a non-Goblin permanent entering is an early no-op.
//
// `entered_index` is the just-entered permanent's battlefield slot; `chosen_tutor` (optional) is a
// search/human-chosen Goblin Matron fetch target (empty -> the provider's TutorCandidates pick).
void PerformMuxusReveal(GameState& state, int controller, const CardParams& pp);   // body in SpellEffects.cpp

// ---- Terastodon ETB-destroy heuristic (USER 2026-08-20) ---------------------------------------
// One lever for the whole tweak (K-set narrowing at emission + the widened victim pool at
// resolution): MTG_TERA_K, DEFAULT ON; =0 restores the v1 shape (full K fan over Forests only).
inline bool TeraKHeuristicEnabled()
{
    static const bool v = EnvOn("MTG_TERA_K", true);
    return v;
}

// Victim ordering class for the ETB self-destroy (lower = eaten first), or -1 = not a valid
// victim. USER: "widen the choice to all valid targets, but only if we run out of forests."
//   0  Forests -- the fungible resource the deck floods (the v1 pool, order preserved)
//   1  other lands (Turntimber back face, Wirewood Lodge)
//   2  mana rocks (Sol Ring)
//   3  other noncreature permanents (Mirri's Guile)
//   4  the reveal engine (Call of the Wild) LAST -- "Worldly Tutor into Call of the Wild
//      activation is a real move for this deck" (USER), so it dies only when nothing else remains.
// Within a class the destroy loop takes tapped before untapped. The widening past class 0 is
// lever-gated; the resolution loop and the emission's target count share THIS predicate so the
// searched K can never exceed what resolution will actually destroy (lockstep by construction).
inline int EtbDestroyVictimClass(const GameState& state, int controller, const Permanent& q)
{
    if (q.controller_index != controller || q.is_animated || q.card.IsCreature()) { return -1; }
    if (q.card.IsLand() && CardHasSubtype(q.card, "Forest")) { return 0; }
    if (!TeraKHeuristicEnabled()) { return -1; }
    if (q.card.IsLand()) { return 1; }
    const CardDefinition* qd = CardDatabase::Instance().LookupCached(q.card);
    if (!qd) { return -1; }
    if (qd->params.activated_reveal_top_cost.has_value()) { return 4; }
    if (qd->params.mana_rock) { return 2; }
    return 3;
}

// How many victims the widened pool holds right now -- the emission's cap on searched K.
inline int CountEtbDestroyTargets(const GameState& state, int controller)
{
    int n = 0;
    for (const Permanent& q : state.battlefield)
    { if (EtbDestroyVictimClass(state, controller, q) >= 0) { ++n; } }
    return n;
}

// kEtbKxHeuristic (declared with the FireOwnEtbTriggers fwd decl above): the PUT paths (Natural
// Order / Call of the Wild / Turntimber) pass it to mean "no searched K rode this entry -- pick
// one heuristically at resolution". Distinct from -1 ("no K specified, destroy nothing") because
// the EXECUTOR's stack entry only records a cast's chosen_x when it is POSITIVE (AIEngine), so a
// searched K=0 hard-cast also arrives as -1 -- overloading -1 would make the executor
// heuristic-override a searched K=0 while the rollout (which passes the int directly) kept it:
// an executor/rollout divergence by construction.

// The ONE Terastodon destroy-K projection, shared by BOTH entry paths (the cast now rides the
// same kEtbKxHeuristic sentinel as the puts, so K is decided HERE at resolution -- mid-plan,
// where the battlefield and the remaining pool already reflect what this plan has done; USER
// 2026-08-21: "we might be able to see it in the current turn plan? ... That would make things
// the easiest"). Defined after AvailableManaPool's declaration; full doctrine at the definition.
inline int ProjectEtbDestroyK(const GameState& state, int controller, const CardDefinition& def);

// ---- CR 700.5 DEVOTION -------------------------------------------------------------------------
// "Your devotion to [color] is the number of mana symbols of that color among the mana costs of
// permanents you control." Counts only PERMANENTS (battlefield), only their printed mana costs,
// and never generic/{C}/{X}. HYBRID pips count for BOTH their colours (CR 202.2b): the engine's
// flat ManaCost bakes a hybrid into its FIRST colour, so that half is already in the flat int and
// only each pip's SECOND colour has to be added here. That makes Boros Reckoner's {R/W}{R/W}{R/W}
// read as devotion-to-red 3 AND devotion-to-white 3, both correct.
//
// A card OUTSIDE the battlefield carries no mask (see the DeckLoader note at the top of this
// header), but every permanent here was built from `def.card`, so its m_mana_cost is real; the
// LookupCached fallback keeps a token (no DB entry, no cost) contributing 0, which is right --
// a token has no mana cost.
inline int DevotionTo(const GameState& state, int controller, const std::string& color)
{
    if (color.empty()) { return 0; }
    const char c = color[0];
    int dev = 0;
    for (const Permanent& q : state.battlefield)
    {
        if (q.controller_index != controller) { continue; }
        const ManaCost& mc = q.card.m_mana_cost;
        switch (c)
        {
            case 'W': dev += mc.white; break;
            case 'U': dev += mc.blue;  break;
            case 'B': dev += mc.black; break;
            case 'R': dev += mc.red;   break;
            case 'G': dev += mc.green; break;
            default: break;
        }
        for (int i = 0; i < mc.hybrid_count; ++i)
        {
            const Color second = static_cast<Color>(mc.hybrid_pair[i] & 0xF);
            const bool  hit =
                  (c == 'W' && second == Color::White) || (c == 'U' && second == Color::Blue)
                || (c == 'B' && second == Color::Black) || (c == 'R' && second == Color::Red)
                || (c == 'G' && second == Color::Green);
            if (hit) { ++dev; }
        }
    }
    return dev;
}

// A permanent just entered under `controller` at battlefield slot `entered_index`: fire ITS OWN
// "When this enters, ..." abilities. Everything below is read off the newcomer's own CardParams,
// which is the line that separates this from FireEtbWatchers (other permanents watching the
// entry) -- see the pair note at the forward declaration.
//
// `chosen_tutor` is the searched target for an ETB tutor; `etb_kx` the searched K for an ETB
// destroy-K, with kEtbKxHeuristic meaning "PUT entry, no searched axis, pick at resolution".
//
// Not Goblin-specific despite the name it carried until 2026-08-30: 26 cards across a dozen decks
// hook these params, and the tribal name is what made a missing call in the search's
// noncreature-permanent projection look inert for months (docs/design/etb-cascade-projection-gap.md).
// Every block is param-gated -> byte-identical for a deck that uses none of them.
inline void FireOwnEtbTriggers(GameState& state, int controller, int entered_index,
                           const std::string& chosen_tutor = "", int etb_kx = -1)
{
    if (entered_index < 0 || entered_index >= static_cast<int>(state.battlefield.size())) { return; }
    const CardDefinition* def =
        CardDatabase::Instance().LookupCached(state.battlefield[entered_index].card);
    if (!def) { return; }
    const CardParams& p = def->params;

    // Peregrine Drake / Cloud of Faeries: "When this creature enters, untap up to N lands." FIRST,
    // ahead of every other own-ETB effect, because it is a MANA effect and a later trigger in the
    // same resolution (Emiel's optional {G/W} counter) may want to spend what it refunds.
    if (p.etb_untap_lands > 0) { EtbUntapLands(state, controller, p.etb_untap_lands); }

    // --- StompySurprise ETB effects (all param-gated; byte-identical elsewhere) ---

    // Elderscale Wurm: "When this creature enters, if your life total is less than N, your life
    // total becomes N."
    if (p.etb_life_floor > 0 && state.players[controller].life < p.etb_life_floor)
    {
        state.players[controller].life = p.etb_life_floor;
    }

    // Craterhoof Behemoth: "creatures you control ... get +X/+X until end of turn, where X is the
    // number of creatures you control" (counted AFTER it enters -> includes itself). Temp bonuses
    // (cleared at cleanup); creatures entering later this turn correctly get nothing (CR 611.2c).
    // The trample grant is inert vs the passive opponent (never blocks) -- disclosed.
    if (p.etb_team_pump_per_creature)
    {
        int x = 0;
        for (const Permanent& q : state.battlefield)
        { if (q.controller_index == controller && (q.card.IsCreature() || q.is_animated)) { ++x; } }
        if (x > 0)
        {
            for (Permanent& q : state.battlefield)
            {
                if (q.controller_index == controller && (q.card.IsCreature() || q.is_animated))
                { q.temp_power_bonus += x; q.temp_tough_bonus += x; }
            }
        }
    }

    // Terastodon: "destroy up to three target noncreature permanents; for each ... its controller
    // creates a 3/3 green Elephant token." The passive opponent controls no noncreature
    // permanents, so the live mode destroys the caster's OWN -- candidates narrowed to own
    // FORESTS, tapped first (fungible; provider-style narrowing, disclosed). K = the searched
    // chosen_x carried on the cast (etb_kx; -1/0 = destroy nothing; kEtbKxHeuristic = "project K
    // here at resolution" -- BOTH the autonomous cast and every put path send it; only human
    // play's explicit fan carries a positive K). Victim order and pool: EtbDestroyVictimClass
    // above (Forests first; the pool widens past them only under MTG_TERA_K).
    if (p.etb_destroy_own_noncreature_max > 0 && etb_kx == kEtbKxHeuristic
        && TeraKHeuristicEnabled())
    { etb_kx = ProjectEtbDestroyK(state, controller, *def); }
    if (p.etb_destroy_own_noncreature_max > 0 && etb_kx > 0)
    {
        int k = std::min(etb_kx, p.etb_destroy_own_noncreature_max);
        int made = 0;
        for (int cls = 0; cls <= 4 && k > 0; ++cls)
        {
            for (int pass = 0; pass < 2 && k > 0; ++pass)
            {
                const bool want_tapped = (pass == 0);
                for (std::size_t i = state.battlefield.size(); i-- > 0 && k > 0; )
                {
                    Permanent& q = state.battlefield[i];
                    if (q.tapped != want_tapped) { continue; }
                    if (EtbDestroyVictimClass(state, controller, q) != cls) { continue; }
                    state.players[q.owner_index].graveyard.push_back(q.card);
                    state.battlefield.erase(state.battlefield.begin() + static_cast<std::ptrdiff_t>(i));
                    --k; ++made;
                }
            }
        }
        for (int t = 0; t < made; ++t)
        {
            CreateToken(state, controller, p.etb_created_token_power,
                        p.etb_created_token_toughness, p.etb_created_token_subtypes,
                        p.created_token_color);
        }
    }

    // (1) ETB fixed-token creation ("create N 1/1 red Goblin tokens" -- Mogg War Marshal 1,
    //     Siege-Gang Commander 3). Uses the shared etb_created_token_* spec.
    for (int k = 0; k < p.etb_self_creates_tokens; ++k)
    {
        CreateToken(state, controller, p.etb_created_token_power,
                    p.etb_created_token_toughness, p.etb_created_token_subtypes,
                    p.created_token_color, HasteKeywords(p.created_token_haste));
    }

    // (1b) ETB OPPONENT-token gift (Hunted Phantasm: "target opponent creates five 1/1 red Goblin
    //      creature tokens"; single opponent -> no target choice). Same etb_created_token_* spec.
    //      Each gift enters through CreateToken -> the universal enter cascade, so it drains
    //      through Suture Priest and feeds Defense of the Heart / Massacre Wurm automatically.
    for (int k = 0; k < p.etb_opp_creates_tokens; ++k)
    {
        CreateToken(state, 1 - controller, p.etb_created_token_power,
                    p.etb_created_token_toughness, p.etb_created_token_subtypes);
    }

    // (1c) ETB opponent-board debuff sweep (Massacre Wurm: "creatures your opponents control get
    //      -2/-2 until end of turn"). The until-EOT toughness reduction is applied for real via
    //      temp_tough_bonus (cleared each cleanup, folded into sim key + board signature), so
    //      TWO sweeps in one turn STACK: a simultaneous double-Wurm put's second trigger kills
    //      at cumulative -4/-4 (a 4/4 spawn dies and drains). The affected set is the creatures
    //      present at THIS resolution (CR 611.2c) -- tokens gifted between sweeps only see later
    //      ones, which falls out of applying the debuff per-permanent here. A creature dies when
    //      effective toughness - marked damage <= 0 (the debuff is not damage). Each kill fires
    //      the opponent-death watchers (the Wurm's own clause 2 counts its own sweep's kills --
    //      it is already on the battlefield when this resolves). Strict gap: -X/-X kills an
    //      indestructible creature only via toughness <= 0; no spawn schedule produces one.
    if (p.etb_opp_creatures_debuff > 0)
    {
        const int opp_ix = 1 - controller;
        for (std::size_t i = state.battlefield.size(); i-- > 0; )
        {
            Permanent& q = state.battlefield[i];
            if (q.controller_index != opp_ix || !(q.card.IsCreature() || q.is_animated)) { continue; }
            q.temp_tough_bonus -= p.etb_opp_creatures_debuff;
            if (q.EffectiveToughness() - q.damage > 0) { continue; }
            const int dead_controller = q.controller_index;
            state.players[q.owner_index].graveyard.push_back(q.card);
            state.battlefield.erase(state.battlefield.begin() + static_cast<std::ptrdiff_t>(i));
            FireOppCreatureDies(state, dead_controller);
        }
    }

    // (2) ETB single-target burn ("deals N damage to any target" -> opponent face; Twinshot 2)
    //     and (3) "each opponent" ping (Chainwhirler 1: face + each opponent creature/pw).
    const int opp = 1 - controller;
    // (2b) ETB devotion burn (Fanatic of Mogis: "deals damage to each opponent equal to your
    //      devotion to red"). Counted AFTER it enters -- the Fanatic is already on the battlefield
    //      when its own ETB trigger resolves, so its own {R} pip counts (CR 700.5 / 603.6a). A
    //      single opponent, so "each opponent" is one face hit. Routed through the same life sink
    //      as the fixed ETB burn so the win projection reads it.
    const int devotion_dmg = p.etb_damage_devotion_color.empty()
                           ? 0
                           : DevotionTo(state, controller, p.etb_damage_devotion_color);
    const int face_dmg = p.etb_damage_any + p.etb_damage_each_opponent + devotion_dmg;
    if (face_dmg > 0)
    {
        const int before = state.players[opp].life;
        state.players[opp].life -= face_dmg;
        state.opponent_lost_life_this_turn = true;
        // Play-viewer history: an ETB burn moves the opponent's life with NOTHING on screen to
        // explain it -- the creature just enters and the total drops. Every other life-changing
        // site emits an event; this one did not, so Fanatic of Mogis's damage was invisible in the
        // history panel (user, 2026-08-29, Minotaur reference seed 1). Devotion especially cannot
        // be recomputed by eye: it counts red pips across EVERY permanent controlled, including
        // the Fanatic itself, so the count is spelled out rather than left as a bare number.
        // Nulled by RevealLogPause during search/rollout -> autonomous play stays byte-identical.
        if (g_play_event_sink)
        {
            EmitPlayEvent(state.turn_number, "damage",
                          "\xF0\x9F\x94\xA5 " + def->card.m_name.str()
                          + (devotion_dmg > 0
                               ? " (devotion to " + p.etb_damage_devotion_color + " = "
                                 + std::to_string(devotion_dmg) + ")"
                               : std::string{})
                          + ": " + std::to_string(face_dmg) + " to opponent ("
                          + std::to_string(before) + "\xE2\x86\x92"
                          + std::to_string(before - face_dmg) + ")");
        }
    }
    if (p.etb_damage_each_opponent > 0)
    {
        // Ping each opponent creature; remove any that die (opponent has creatures only from spawn
        // effects, so this is a no-op in the usual passive goldfish). No SBA re-entrancy: we prune
        // inline. entered_index belongs to `controller`, never the opponent, so it is untouched.
        for (std::size_t i = state.battlefield.size(); i-- > 0; )
        {
            Permanent& q = state.battlefield[i];
            if (q.controller_index != opp || !q.card.IsCreature()) { continue; }
            q.damage += p.etb_damage_each_opponent;
            if (q.damage >= q.EffectiveToughness()
                && !q.card.HasKeyword(Keyword::Indestructible))
            {
                const int dead_controller = q.controller_index;
                state.players[q.owner_index].graveyard.push_back(q.card);
                state.battlefield.erase(state.battlefield.begin() + static_cast<std::ptrdiff_t>(i));
                // Opponent-death watchers (Massacre Wurm clause 2). Param-gated scan ->
                // byte-identical for every deck without a watcher (incl. all Goblins GT).
                FireOppCreatureDies(state, dead_controller);
            }
        }
    }

    // (4) ETB tutor to hand keyed on subtype (Goblin Matron). Reuses PerformTutor; the chosen
    //     target rides in from the cast (search/human) or falls back to the provider's pick.
    if ((p.tutor_to_hand || p.tutor_to_top) && !p.tutor_types.empty()
        && p.etb_reveal_count == 0)   // Muxus (reveal) is handled below, not as a tutor
    {
        PerformTutor(state, controller, p, chosen_tutor, def->card.m_name.str());
    }

    // (5) ETB reveal-and-cheat (Muxus): reveal top N, put matching creatures MV<=k onto the
    //     battlefield (each firing its OWN ETB via this same cascade), rest to the library bottom.
    if (p.etb_reveal_count > 0)
    {
        PerformMuxusReveal(state, controller, p);
    }
}


// ---- Goblins tribal death-watcher ("whenever [this or] another <subtype> you control dies") ------
// `dead_card` (a creature controlled by `dead_controller`) has just LEFT the battlefield and must
// ALREADY be removed before calling. Fires, for every matching watcher, its effect: damage to the
// opponent's face, token creation, and/or impulse-exile of the top library card. Called at every
// death site -- the executor SBA (GameEngine::CheckStateBasedActions) and each sacrifice outlet
// (lockstep executor + rollout). Gated: a death with no watcher in play is a no-op.
//
// Approximation (documented): simultaneous multi-death (two watched creatures dying in the SAME SBA
// pass) resolves each against the survivors, so a watcher that dies alongside another watched
// creature does not see that co-death. This is essentially unreachable in goldfishing (our creatures
// die one-at-a-time via sacrifice outlets, never in combat vs the passive opponent).
// ---- SACRIFICE watchers ("whenever you sacrifice a permanent, ...") ----------------------------
// Slaughter-Priest of Mogis: "Whenever you sacrifice a permanent, this creature gets +2/+0 until
// end of turn." Distinct from OnCreatureDies below in BOTH directions: a sacrifice is not always a
// death (a sacrificed LAND is not a creature dying) and a death is not always a sacrifice (combat
// damage, a -X/-X sweep). So this is its own hook, called from every site where the controller
// SACRIFICES one of their own permanents -- the fetchland crack, the creature-sac outlets, a
// sacrifice paid as an additional cost, and Lotus Bloom's tap-and-sac. Call it ONCE PER SACRIFICED
// PERMANENT, after the permanent has left the battlefield.
//
// Gated on a watcher being in play, so every deck without one pays a single battlefield scan whose
// body never runs -> byte-identical. Note the interaction that makes this live in the Minotaur
// deck: cracking a Bloodstained Mire IS a sacrifice, so a fetch played on the attacking turn pumps
// the Priest for free.
inline void FireSacrificeWatchers(GameState& state, int controller)
{
    for (Permanent& w : state.battlefield)
    {
        if (w.controller_index != controller) { continue; }
        const CardDefinition* wd = CardDatabase::Instance().LookupCached(w.card);
        if (!wd || wd->params.sacrifice_watch_pump_power <= 0) { continue; }
        w.temp_power_bonus += wd->params.sacrifice_watch_pump_power;
        if (g_play_event_sink && !g_tap_speculating)
        {
            EmitPlayEvent(state.turn_number, "trigger",
                          w.card.m_name.str() + ": +"
                          + std::to_string(wd->params.sacrifice_watch_pump_power)
                          + "/+0 (a permanent was sacrificed)");
        }
    }
}

inline void OnCreatureDies(GameState& state, int dead_controller, const Card& dead_card,
                           bool dead_was_token = false)
{
    std::vector<CardParams> reactions;
    // Other watchers still in play under the same controller: "another <subtype> you control dies".
    for (const Permanent& w : state.battlefield)
    {
        if (w.controller_index != dead_controller) { continue; }
        const CardDefinition* wd = CardDatabase::Instance().LookupCached(w.card);
        if (!wd) { continue; }
        const CardParams& wp = wd->params;
        if (wp.dies_watch_subtype.empty()) { continue; }   // self-only watchers can't fire for others
        if (CardHasSubtype(dead_card, wp.dies_watch_subtype)) { reactions.push_back(wp); }
    }
    // The dead creature's OWN watcher (self death), if it includes itself.
    {
        const CardDefinition* dd = CardDatabase::Instance().LookupCached(dead_card);
        if (dd && dd->params.dies_watch_includes_self)
        {
            const CardParams& dp = dd->params;
            const bool self_ok = dp.dies_watch_subtype.empty()
                              || CardHasSubtype(dead_card, dp.dies_watch_subtype);
            if (self_ok) { reactions.push_back(dp); }
        }
    }
    // Worldspine Wurm: "When ~ is put into a graveyard from anywhere, shuffle it into its owner's
    // library." A TRIGGER (not a replacement -- it does hit the graveyard, so its dies-triggers
    // below still fire; Progenitus' cleanup-discard REPLACEMENT wiring stays separate). The death
    // site pushed the card into the graveyard before calling here; pull the newest matching copy
    // back and shuffle it in. Runs regardless of `reactions` (before the early-out). A token copy
    // ceases to exist instead (CR 111.7) -- gated on !dead_was_token.
    if (!dead_was_token)
    {
        const CardDefinition* dd = CardDatabase::Instance().LookupCached(dead_card);
        if (dd && dd->params.graveyard_replace_shuffle_library)
        {
            std::vector<Card>& gy = state.players[dead_controller].graveyard;
            for (std::size_t g = gy.size(); g-- > 0; )
            {
                if (gy[g].m_name == dead_card.m_name)
                {
                    Card back = gy[g];
                    gy.erase(gy.begin() + static_cast<std::ptrdiff_t>(g));
                    state.players[dead_controller].library.push_back(std::move(back));
                    ShuffleAfterSearch(state, dead_controller);
                    break;
                }
            }
        }
    }

    if (reactions.empty()) { return; }

    const int opp = 1 - dead_controller;
    Player&   ap  = state.players[dead_controller];
    for (const CardParams& wp : reactions)
    {
        if (wp.dies_trigger_damage > 0)
        {
            state.players[opp].life -= wp.dies_trigger_damage;   // "1 damage to any target" -> face
            state.opponent_lost_life_this_turn = true;
        }
        for (int k = 0; k < wp.dies_trigger_creates_tokens; ++k)
        {
            CreateToken(state, dead_controller, wp.dies_token_power,
                        wp.dies_token_toughness, wp.dies_token_subtypes,
                        wp.created_token_color);
        }
        // Vaultborn Tyrant: "When this creature dies, if it's not a token, create a token that's
        // a copy of it." Only ever in `reactions` via the dead card's OWN self-watcher, so the
        // copy is of dead_card itself. The copy's enter fires the watchers (gain 3 / draw 1).
        if (wp.dies_trigger_copy_self_token && !dead_was_token)
        {
            CreateTokenCopyOfCard(state, dead_controller, dead_card);
        }
        if (wp.dies_trigger_impulse_exile && !ap.library.empty())
        {
            // "Exile the top card; if it's a <type> <subtype> card, you may cast it until end of your
            // next turn" (Rundvelt: Goblin creature). A match is staged into hand (playable, expiry);
            // a non-match stays exiled and inert (nothing reads exile in goldfish -> deck thinning).
            Card c = ap.library.DrawTop();
            const CardDefinition* cd = CardDatabase::Instance().LookupCached(c);
            const Card& cc = cd ? cd->card : c;
            bool ok = true;
            if (!wp.dies_impulse_requires_type.empty())
            { ok = ok && CardMatchesTypeName(cc, wp.dies_impulse_requires_type); }
            if (!wp.dies_impulse_requires_subtype.empty())
            { ok = ok && CardHasSubtype(cc, wp.dies_impulse_requires_subtype); }
            if (ok)
            {
                c.m_is_staged     = true;
                c.m_staged_expiry = state.turn_number + (wp.dies_impulse_expiry_next_turn ? 1 : 0);
                ap.hand.push_back(std::move(c));
            }
        }
    }
}

// ---- Goblins tribal activated outlets (Krenko tap / sac-a-Goblin / Twinshot channel) -----------
// Count creatures `controller` controls whose subtype includes `sub` (tokens count; they carry the
// subtype). Used by Krenko's X and Piledriver-style scans.
inline int CountControlledSubtype(const GameState& state, int controller, const std::string& sub)
{
    int n = 0;
    for (const Permanent& p : state.battlefield)
    { if (p.controller_index == controller && p.card.IsCreature() && CardHasSubtype(p.card, sub)) { ++n; } }
    return n;
}

// Krenko, Mob Boss "{T}: Create X 1/1 Goblins, X = Goblins you control." Tap the source, then create
// X tokens (X counted at resolution, including Krenko himself + existing tokens). Lockstep both worlds.
inline void ApplyTapForTokens(GameState& state, int controller, int source_id)
{
    const CardDefinition* d = nullptr;
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller || p.tapped) { continue; }
        if (p.card.m_number != source_id) { continue; }
        const CardDefinition* pd = CardDatabase::Instance().LookupCached(p.card);
        if (!pd || pd->params.tap_creates_tokens_per_controlled_subtype.empty()) { continue; }
        p.tapped = true;   // {T} cost
        d = pd;
        break;
    }
    if (!d) { return; }
    const int x = CountControlledSubtype(state, controller,
                                         d->params.tap_creates_tokens_per_controlled_subtype);
    for (int k = 0; k < x; ++k)   // CreateToken reallocates battlefield; `d` (DB cache ptr) stays valid
    {
        CreateToken(state, controller, d->params.tap_created_token_power,
                    d->params.tap_created_token_toughness, d->params.tap_created_token_subtypes);
    }
}

// ---- Garth One-Eye: choose an un-chosen name, conjure the copy, cast it as the ability resolves -
// The six names (mask bit order): Disenchant(0), Braingeyser(1), Terror(2), Shivan Dragon(3),
// Regrowth(4), Black Lotus(5). The enumeration only offers names whose cast is affordable and
// goldfish-live NOW (Disenchant never -- structurally target-less, user-approved stub), so this
// apply both marks the name chosen AND casts the copy. The copy's mana cost was reserved by the
// plan's subset math (a.cost) and is paid here via the caller's TapForCost* (except Black Lotus,
// {0}). Braingeyser's X is auto-maxed from the mana left after payment of {U}{U} (disclosed).
inline int GarthNameBit(const std::string& name)
{
    if (name == "Disenchant")    { return 0; }
    if (name == "Braingeyser")   { return 1; }
    if (name == "Terror")        { return 2; }
    if (name == "Shivan Dragon") { return 3; }
    if (name == "Regrowth")      { return 4; }
    if (name == "Black Lotus")   { return 5; }
    return -1;
}

inline void ApplyGarthActivate(GameState& state, int controller, int garth_id,
                               const std::string& name, int braingeyser_x)
{
    const int bit = GarthNameBit(name);
    if (bit < 0) { return; }
    Permanent* garth = nullptr;
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller || p.card.m_number != garth_id) { continue; }
        const CardDefinition* pd = CardDatabase::Instance().LookupCached(p.card);
        if (!pd || !pd->params.garth_copy_ability) { continue; }
        if (p.tapped || !p.CanTap()) { return; }                       // stranded: no-op
        if (p.garth_chosen_mask & (1u << bit)) { return; }             // name already chosen
        garth = &p;
        break;
    }
    if (!garth) { return; }
    garth->tapped = true;
    garth->garth_chosen_mask |= static_cast<uint8_t>(1u << bit);

    const CardDefinition* cd = CardDatabase::Instance().Lookup(name);
    if (!cd) { return; }
    // The copy is CAST: it counts toward storm-style counters and fires on-cast triggers
    // (a multicolored conjure would ping Mana Cannons -- none of the six is multicolored).
    ++state.spells_cast_this_turn;
    FireOnCastTriggers(state, *cd);

    if (name == "Black Lotus" || name == "Shivan Dragon")
    {
        // Permanent copies enter the battlefield (Lotus's sac-for-mana and Shivan's
        // firebreathing are param-driven from their cards.json entries; the token-copy is a new
        // object with a fresh token id -- see GameState::next_token_number).
        Permanent perm;
        perm.card              = cd->card;
        perm.card.m_number     = state.next_token_number++;
        perm.controller_index  = controller;
        perm.owner_index       = controller;
        perm.entered_this_turn = true;
        perm.is_token          = true;   // a conjured copy ceases to exist in other zones anyway
        state.battlefield.push_back(perm);
        FireEtbWatchers(state, controller, static_cast<int>(state.battlefield.size()) - 1);
    }
    else if (name == "Braingeyser")
    {
        Player& pl = state.players[controller];
        std::size_t before = pl.hand.size();
        for (int k = 0; k < braingeyser_x && !pl.library.empty(); ++k)
        { pl.library.DrawN(1, pl.hand); }
        pl.cards_drawn_this_turn += static_cast<int>(pl.hand.size() - before);
        if (g_play_draw_sink)
        {
            for (std::size_t hi = before; hi < pl.hand.size(); ++hi)
            { g_play_draw_sink->push_back({ state.turn_number, pl.hand[hi].m_name.str() }); }
        }
    }
    else if (name == "Regrowth")
    {
        // Return the highest-MV graveyard card to hand (AUTO-RESOLVED pick, disclosed).
        std::vector<Card>& gy = state.players[controller].graveyard;
        int best = -1, best_mv = -1;
        for (int g = 0; g < static_cast<int>(gy.size()); ++g)
        {
            const int mv = ZoneCard(gy[g]).m_mana_cost.ManaValue();   // placeholder cost is empty
            if (mv > best_mv) { best_mv = mv; best = g; }
        }
        if (best >= 0)
        {
            state.players[controller].hand.push_back(gy[best]);
            gy.erase(gy.begin() + best);
        }
    }
    else if (name == "Terror")
    {
        // Destroy the largest opponent nonartifact (spawn tokens are colorless, never black)
        // creature. Payoff ~0 vs a passive opponent, but faithful.
        int pick = -1, best_pw = -1;
        for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
        {
            const Permanent& q = state.battlefield[i];
            if (q.controller_index == controller || !q.card.IsCreature()) { continue; }
            if (q.card.HasType(CardType::Artifact) || q.card.HasColor(Color::Black)) { continue; }
            const int pw = q.EffectivePower();
            if (pw > best_pw) { best_pw = pw; pick = i; }
        }
        if (pick >= 0)
        {
            const Permanent dead = state.battlefield[pick];
            if (!dead.is_token)
            { state.players[dead.owner_index].graveyard.push_back(dead.card); }
            state.battlefield.erase(state.battlefield.begin() + pick);
        }
    }
    // Disenchant: never enumerated (user-approved choose-but-never-cast stub).
}

// ---- Planeswalker loyalty activation (Jared Carthalion / Nicol Bolas / Oko) ---------------------
// One loyalty ability per walker per turn (CR 606.3), sorcery-speed, usable the turn the walker
// enters. Pays the loyalty delta first (CR 606.5); a walker left at loyalty <= 0 goes to its
// owner's graveyard immediately (the only loyalty-death path vs a passive opponent -- nothing
// else ever damages a walker here). Effects are scripted primitives; auto-resolved sub-choices
// (which creatures get Jared's counters, which card Regrowth returns, which own permanent Bolas
// destroys, which Food becomes an Elk) use deterministic provably-reasonable picks, disclosed in
// the deck's 6a table. Applied identically by the rollout and executor trailing passes (lockstep).
// `elk_target` = the card.m_number of the permanent Oko's +1 turns into a 3/3 Elk (0 = the legacy
// "first own Food Token" pick). SEARCHED, not hardcoded: see the enumeration note in TurnSolver.
inline void ApplyLoyaltyAbility(GameState& state, int controller, int walker_id, int ability_index,
                                int elk_target = 0)
{
    int wi = -1;
    const CardDefinition* d = nullptr;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        Permanent& p = state.battlefield[i];
        if (p.controller_index != controller || p.card.m_number != walker_id) { continue; }
        const CardDefinition* pd = CardDatabase::Instance().LookupCached(p.card);
        if (!pd || pd->params.loyalty_abilities.empty()) { continue; }
        if (p.loyalty_activated_this_turn) { return; }   // stranded duplicate: no-op
        wi = i; d = pd;
        break;
    }
    if (wi < 0 || !d) { return; }
    if (ability_index < 0
        || ability_index >= static_cast<int>(d->params.loyalty_abilities.size())) { return; }
    const CardParams::LoyaltyAbilityParam& ab = d->params.loyalty_abilities[ability_index];
    Permanent& w = state.battlefield[wi];
    if (ab.delta < 0 && w.loyalty < -ab.delta) { return; }   // can't pay the cost
    w.loyalty += ab.delta;
    w.loyalty_activated_this_turn = true;
    for (Counter& c : w.counters)
    {
        if (c.type == Counter::Type::Loyalty) { c.count = w.loyalty; break; }   // viewer mirror
    }

    auto make_token = [&](const char* nm, bool creature, int pw, int tf,
                          const std::vector<std::string>& subs, bool all_colors, bool green_only,
                          bool trample)
    {
        Permanent token;
        token.card.m_name = nm;
        token.card.RehashName();
        token.card.AddType(creature ? CardType::Creature : CardType::Artifact);
        token.card.m_subtypes = subs;
        if (creature) { token.card.m_power = pw; token.card.m_toughness = tf; }
        if (trample)  { token.card.AddKeyword(Keyword::Trample); }
        if (all_colors)
        {
            token.card.AddColor(Color::White); token.card.AddColor(Color::Blue);
            token.card.AddColor(Color::Black); token.card.AddColor(Color::Red);
            token.card.AddColor(Color::Green);
        }
        if (green_only) { token.card.AddColor(Color::Green); }
        token.card.m_number     = state.next_token_number++;   // unique per-copy id (GameState.h note)
        token.controller_index  = controller;
        token.owner_index       = controller;
        token.entered_this_turn = true;
        token.is_token          = true;
        state.battlefield.push_back(token);
    };

    if (ab.effect == "kavu_token")
    {
        // Jared +1: a 3/3 Kavu with trample that's ALL colors (its colors feed domain mana,
        // Jared's -3 counter count, and the -6 all-colors check on other cards).
        make_token("Kavu Token", true, 3, 3, {"Kavu"}, /*all_colors=*/true, false, /*trample=*/true);
    }
    else if (ab.effect == "counters_up_to_two")
    {
        // Jared -3: up to `amount` target creatures each get +1/+1 counters equal to their color
        // count. AUTO-RESOLVED collapse: the amount highest-color-count own creatures (total
        // stats are additive vs a never-blocking opponent, so max-total is provably optimal;
        // ties by battlefield order). Disclosed 6a.
        std::vector<std::pair<int, int>> ranked;   // (color_count, index)
        for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
        {
            const Permanent& q = state.battlefield[i];
            if (q.controller_index != controller || !q.card.IsCreature()) { continue; }
            const int cc = q.card.ColorCount();
            if (cc > 0) { ranked.push_back({cc, i}); }
        }
        std::stable_sort(ranked.begin(), ranked.end(),
                         [](const std::pair<int, int>& x, const std::pair<int, int>& y)
                         { return x.first > y.first; });
        for (int k = 0; k < ab.amount && k < static_cast<int>(ranked.size()); ++k)
        {
            state.battlefield[ranked[k].second].counters.push_back(
                Counter{Counter::Type::PlusOnePlusOne, ranked[k].first});
        }
    }
    else if (ab.effect == "regrow_multicolored")
    {
        // Jared -6: return the highest-MV multicolored card from the graveyard to hand
        // (AUTO-RESOLVED pick, disclosed); if it was ALL colors, draw a card and make two
        // Treasures ("Treasure Token" has a cards.json entry -> its sac-for-mana is live).
        std::vector<Card>& gy = state.players[controller].graveyard;
        int best = -1, best_mv = -1;
        for (int g = 0; g < static_cast<int>(gy.size()); ++g)
        {
            const Card& gc = ZoneCard(gy[g]);   // placeholder colour/cost masks are empty
            if (!gc.IsMulticolored()) { continue; }
            const int mv = gc.m_mana_cost.ManaValue();
            if (mv > best_mv) { best_mv = mv; best = g; }
        }
        if (best >= 0)
        {
            Card back = gy[best];
            gy.erase(gy.begin() + best);
            const bool all5 = ZoneCard(back).ColorCount() == 5;
            state.players[controller].hand.push_back(std::move(back));
            if (all5)
            {
                Player& pl = state.players[controller];
                if (!pl.library.empty())
                {
                    std::size_t before = pl.hand.size();
                    pl.library.DrawN(1, pl.hand);
                    pl.cards_drawn_this_turn += static_cast<int>(pl.hand.size() - before);
                    if (g_play_draw_sink)
                    {
                        for (std::size_t hi = before; hi < pl.hand.size(); ++hi)
                        { g_play_draw_sink->push_back({ state.turn_number, pl.hand[hi].m_name.str() }); }
                    }
                }
                make_token("Treasure Token", false, 0, 0, {"Treasure"}, false, false, false);
                make_token("Treasure Token", false, 0, 0, {"Treasure"}, false, false, false);
            }
        }
    }
    else if (ab.effect == "destroy_own_noncreature")
    {
        // Bolas +3 ("destroy target noncreature permanent"). The ability REQUIRES a target, so
        // with a permanent-less opponent the only legal ones were OURS -- and destroying our own
        // land is a real cost the search rightly declined. Since -9 is unreachable from loyalty 5
        // without two +3s, that made an EIGHT-mana walker inert BY CONSTRUCTION (USER, 2026-08-16:
        // "you need a target to use the ability and destroying your own stuff is bad").
        // FiveColourProvider::OpponentPlaysLands now gives the passive opponent lands, so the
        // faithful play is available and this resolves to THEIR permanent first: free loyalty ramp,
        // and their lands are inert props so nothing else about the game changes.
        int pick = -1;
        // Pass 1 -- an OPPONENT noncreature permanent (the real MTG play, and free).
        for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
        {
            const Permanent& q = state.battlefield[i];
            if (q.controller_index == controller || q.card.IsCreature()) { continue; }
            pick = i;
            break;
        }
        // Pass 2 -- fall back to our own only if they have none. AUTO-RESOLVED (disclosed): the
        // most REPLACEABLE own permanent. A token (Food / Treasure) goes before a land: the old
        // rule took the first LAND in battlefield order and called it "most replaceable", but a
        // land is the LEAST replaceable noncreature permanent in a five-colour manabase, and
        // battlefield order makes that the earliest-played land -- usually the key fixer.
        if (pick < 0)
        {
            int land_pick = -1, other_pick = -1;
            for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
            {
                const Permanent& q = state.battlefield[i];
                if (q.controller_index != controller || q.card.IsCreature()) { continue; }
                if (q.card.m_number == walker_id) { continue; }
                if (q.card.HasType(CardType::Planeswalker)) { continue; }
                if (q.is_token) { pick = i; break; }              // most replaceable
                if (q.card.IsLand()) { if (land_pick < 0) { land_pick = i; } continue; }
                if (other_pick < 0) { other_pick = i; }
            }
            // Token first is the only ordering change; land-before-other keeps the historical rule
            // (which of a Cornucopia / Mana Cannons / a dual is "most replaceable" is a judgement
            // this fallback -- reachable only once the opponent's lands are gone -- cannot earn).
            if (pick < 0) { pick = land_pick >= 0 ? land_pick : other_pick; }
        }
        if (pick >= 0)
        {
            Permanent dead = state.battlefield[pick];
            for (Permanent& q : state.battlefield)
            { if (q.equipped_to == dead.card.m_number) { q.equipped_to = 0; } }
            if (!dead.is_token)
            { state.players[dead.owner_index].graveyard.push_back(dead.card); }
            state.battlefield.erase(state.battlefield.begin() + pick);
        }
    }
    else if (ab.effect == "steal_creature")
    {
        // Bolas -2 ("gain control of target creature"). Target is SEARCHED (elk_target parameter,
        // shared with Oko's +1); 0 falls back to the opponent's highest-power creature.
        int pick = -1, best_pow = -1;
        for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
        {
            const Permanent& q = state.battlefield[i];
            if (q.controller_index == controller || !q.card.IsCreature()) { continue; }
            if (elk_target != 0) { if (q.card.m_number == elk_target) { pick = i; break; } continue; }
            const int pw = q.EffectivePower();
            if (pw > best_pow) { best_pow = pw; pick = i; }
        }
        if (pick >= 0)
        {
            Permanent& got = state.battlefield[pick];
            got.controller_index = controller;
            // CR 302.6: summoning sickness tracks CONTROL duration. The spawn was created with
            // entered_this_turn = false ("already present"), so WITHOUT this it would attack the
            // very turn it was stolen. USER, 2026-08-16. Haste would still override.
            got.gained_control_this_turn = true;
            got.tapped                   = false;
        }
    }
    else if (ab.effect == "face_damage")
    {
        // Bolas -9: 7 damage to the opponent's face (the discard-7 / sacrifice-7 riders are
        // inert vs a hand-less opponent whose only permanents are spawn tokens; user-approved).
        state.players[1 - controller].life -= ab.amount;
        state.opponent_lost_life_this_turn = true;
    }
    else if (ab.effect == "food_token")
    {
        // Oko +2: a Food artifact token. Its own "{2},{T},Sac: gain 3 life" ability is a
        // user-approved inert deferral; the token's modeled value is Elk fodder (+1) and body
        // count.
        make_token("Food Token", false, 0, 0, {"Food"}, false, false, false);
    }
    else if (ab.effect == "elk_transform")
    {
        // Oko +1: "target artifact or creature loses all abilities and becomes a green Elk
        // creature with base power and toughness 3/3."
        //
        // The target is SEARCHED (elk_target), not hardcoded. It used to be narrowed to "the first
        // own Food Token", on the stated premise that "Elking a real creature is strictly worse" --
        // which is false, and on FiveColour badly so (USER 2026-08-16). The deck's creatures are
        // 0/1 mana dorks: Elking a Birds of Paradise yields a 3/3 that ATTACKS THAT TURN, because
        // summoning sickness tracks how long you have CONTROLLED the permanent (CR 302.6), not how
        // long it has been a creature. So the swap is a real trade the search must weigh -- +3
        // power now against losing a mana source ("loses all abilities" kills the mana ability) --
        // and hardcoding it away made Oko look like it did nothing pre-combat.
        //
        // entered_this_turn is preserved either way (a Food made THIS turn still makes a
        // summoning-sick Elk, faithful).
        int ti = -1;
        for (int qi = 0; qi < static_cast<int>(state.battlefield.size()); ++qi)
        {
            const Permanent& q = state.battlefield[qi];
            if (q.controller_index != controller) { continue; }
            if (elk_target != 0)
            {
                if (q.card.m_number != elk_target) { continue; }
                if (!q.card.IsCreature() && !q.card.HasType(CardType::Artifact)) { continue; }
            }
            else if (q.card.m_name.str() != "Food Token") { continue; }
            ti = qi; break;
        }
        if (ti >= 0)
        {
            Permanent& q = state.battlefield[ti];
            q.card.m_name = "Elk";
            q.card.RehashName();
            q.card.m_type_mask    = 0;
            q.card.AddType(CardType::Creature);
            q.card.m_subtypes     = {"Elk"};
            q.card.m_power        = 3;
            q.card.m_toughness    = 3;
            q.card.m_keyword_mask = 0;
            q.card.m_color_mask   = 0;
            q.card.AddColor(Color::Green);
            // Counters/temp buffs are NOT cleared: the Elk keeps them (CR 613 -- the P/T-setting
            // effect applies in layer 7b, +1/+1 counters in 7d, so a countered Elk is bigger).
        }
    }

    // Loyalty cost paid the walker to death (a minus ability at exactly its loyalty): CR 704.5i.
    // Re-locate by id first -- the effect above may have reallocated (token push_back) or shifted
    // (destroy erase) the battlefield, so `wi`/`w` are no longer trustworthy.
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        Permanent& q = state.battlefield[i];
        if (q.controller_index != controller || q.card.m_number != walker_id) { continue; }
        if (q.loyalty > 0) { break; }
        const Card dead_card = q.card;
        state.players[q.owner_index].graveyard.push_back(dead_card);
        state.battlefield.erase(state.battlefield.begin() + i);
        break;
    }
}

// ---- Equipment attach (Lightning Greaves) -------------------------------------------------------
// Equip: sorcery-speed re-point of an is_equipment permanent onto a controlled creature (CR 701.3;
// equip ATTACHES, it does not target -- the goldfish has no targeting restrictions anyway). The
// generic cost is paid by the caller (the trailing outlet apply pass, both worlds); {0} for
// Greaves. No-op if either permanent is missing (stranded-action safe, lockstep).
// Metalcraft support (Puresteel Paladin). Artifact count for `controller` -- Equipment ARE
// artifacts, so this deck's 13 equips + Sol Ring flip the threshold from the third artifact on
// (the Paladin itself, a creature, does not count).
inline int CountControlledArtifacts(const GameState& state, int controller)
{
    int n = 0;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller) { continue; }
        if (p.card.HasType(CardType::Artifact)) { ++n; }
    }
    return n;
}

// The equip cost IN EFFECT right now: {0} while any metalcraft reducer (Puresteel Paladin,
// metalcraft_equip_zero_artifacts > 0) is on the battlefield with its artifact threshold met,
// else the printed equip_cost_generic. Called at BOTH enumeration (TurnSolver's Equip candidate
// block) and every payment site (rollout apply_one, ApplyManaUnlockEquips, the executor) so a
// mid-plan metalcraft flip -- cast artifact #3, then equip -- stays lockstep. Multiple Paladins
// are redundant, not cumulative (a static ability either applies or doesn't).
inline int EquipCostGenericNow(const GameState& state, int controller,
                               const CardDefinition& equip_def)
{
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d || d->params.metalcraft_equip_zero_artifacts <= 0) { continue; }
        if (CountControlledArtifacts(state, controller)
            >= d->params.metalcraft_equip_zero_artifacts) { return 0; }
    }
    return equip_def.params.equip_cost_generic;
}

// The Equip action's cost recomputed AT PAYMENT time. The enumeration bakes EquipCostGenericNow
// into Action::cost, but metalcraft (Puresteel) can flip between enumeration and apply -- a plan
// that casts artifact #3 then equips must pay {0}, not the stale baked cost. All three payment
// sites (rollout apply_one, ApplyManaUnlockEquips, the AIEngine executor) call this so they can
// never disagree. Falls back to the baked cost if the equipment is not on the battlefield.
inline ManaCost EquipActionCostNow(const GameState& state, int controller, int equip_id,
                                   const ManaCost& baked)
{
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller || p.card.m_number != equip_id) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.is_equipment)
        {
            ManaCost c;
            c.generic = EquipCostGenericNow(state, controller, *d);
            return c;
        }
    }
    return baked;
}

// A host's power as the O-Naginata attach gate sees it: base + lords + auras + ALREADY-attached
// equipment (the incoming equipment is not yet attached, so EquipBonusFor naturally excludes
// it). Shared by the enumeration filter and ApplyEquip's defense-in-depth check below so the
// two can never disagree about a legal host.
inline int EquipGatePowerOf(const Permanent& host, const GameState& state)
{
    int pw = host.EffectivePower()
           + ComputeLordBonus(host.card, state, host.controller_index,
                              host.is_animated, &host).first
           + AuraBonusFor(host, state).first
           + EquipBonusFor(host, state).first;
    return pw;
}

inline void SacrificePermanentAt(GameState& state, int controller, int idx);  // defined below

// Will ApplyEquip below actually ATTACH, or silently no-op? One predicate, so a caller that must
// decide BEFORE paying an equip cost cannot drift from what the apply really does.
//
// It exists because the executor and the rollout both paid FIRST and applied second: TapForCost
// runs, then ApplyEquip finds no legal host and returns, so the cost buys nothing and (in the
// executor) a log line claims an attach the board does not show. Measured on KittyEquipment: a plan
// co-selecting {cast Puresteel Paladin, equip Bonesplitter -> Paladin} whose CAST was dropped as
// unpayable still paid the equip. See docs/design/equip-host-not-on-battlefield.md.
inline bool CanAttachEquip(const GameState& state, int controller, int equip_id, int creature_id)
{
    const Permanent* eq   = nullptr;
    const Permanent* host = nullptr;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller) { continue; }
        if (p.card.m_number == equip_id)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (d && d->params.is_equipment) { eq = &p; }
        }
        if (p.card.m_number == creature_id && (p.card.IsCreature() || p.is_animated)) { host = &p; }
    }
    if (!eq || !host) { return false; }
    // O-Naginata's "power 3 or greater" gate, evaluated exactly where ApplyEquip evaluates it
    // (the host's power counts already-attached equipment, not the one being placed).
    const CardDefinition* eqd = CardDatabase::Instance().LookupCached(eq->card);
    if (eqd && eqd->params.equip_min_power > 0
        && EquipGatePowerOf(*host, state) < eqd->params.equip_min_power) { return false; }
    return true;
}

inline void ApplyEquip(GameState& state, int controller, int equip_id, int creature_id)
{
    Permanent* eq = nullptr;
    const CardDefinition* eqd = nullptr;
    bool host_ok = false;
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller) { continue; }
        if (p.card.m_number == equip_id)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (d && d->params.is_equipment) { eq = &p; eqd = d; }
        }
        if (p.card.m_number == creature_id && (p.card.IsCreature() || p.is_animated))
        { host_ok = true; }
    }
    if (!eq || !host_ok) { return; }
    // O-Naginata: "can be attached only to a creature with power 3 or greater" (CR 701.3c: an
    // illegal target makes the equip a no-op). Defense in depth -- the Equip enumeration already
    // filters, this keeps a stale/hand-built plan honest.
    if (eqd->params.equip_min_power > 0)
    {
        const Permanent* host = nullptr;
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index == controller && p.card.m_number == creature_id)
            { host = &p; break; }
        }
        if (!host || EquipGatePowerOf(*host, state) < eqd->params.equip_min_power) { return; }
    }
    // Grafted Wargear (per the 2020-11-10 ruling): only a genuine re-host -- moving to a
    // DIFFERENT creature -- sacrifices the former host; first attach and host-death unattach
    // resolve as no-ops. Lives here, on the SHARED attach path, so a normal Equip and Balan's
    // attach-all fire it identically.
    const int prior = eq->equipped_to;
    if (eqd->params.equip_sacrifices_prior_host && prior != 0 && prior != creature_id)
    {
        int idx = -1;
        for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
        {
            if (state.battlefield[i].controller_index == controller
                && state.battlefield[i].card.m_number == prior) { idx = i; break; }
        }
        if (idx >= 0)
        {
            SacrificePermanentAt(state, controller, idx);
            // The erase shifted/reallocated the battlefield -- re-find the equipment before
            // writing (and bail if the sacrifice somehow took it out too).
            eq = nullptr;
            for (Permanent& p : state.battlefield)
            {
                if (p.controller_index == controller && p.card.m_number == equip_id)
                { eq = &p; break; }
            }
            if (!eq) { return; }
        }
    }
    eq->equipped_to = creature_id;
}

// True when `equip_id` is ALREADY attached to `creature_id`. The Equip enumeration never offers a
// pair that is already attached, so DURING a plan application this can only mean the mana-unlock
// hoist (TurnSolver::ApplyManaUnlockEquips) fired it mid-casts -- which is how the trailing equip
// pass in the rollout and the executor knows to skip it (re-firing would pay the cost a 2nd time).
inline bool EquipmentAttachedTo(const GameState& state, int controller, int equip_id, int creature_id)
{
    if (equip_id <= 0 || creature_id <= 0) { return false; }
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller) { continue; }
        if (p.card.m_number == equip_id) { return p.equipped_to == creature_id; }
    }
    return false;
}

// ---- Deathrite Shaman graveyard-exile value outlets (abilities 2/3) -----------------------------
// "{B},{T}: exile an instant/sorcery card from a graveyard -> each opponent loses 2 life" (mode 1)
// "{G},{T}: exile a creature card from a graveyard -> you gain 2 life"                     (mode 2)
// The colored cost is paid by the caller (mirrors SacCreatureOutlet's trailing apply pass, both
// worlds); here we tap the source, exile the first matching fuel card from the controller's own
// graveyard (fungible within the type filter -- deterministic in both worlds), and realise the
// effect. A source that is tapped/missing or has no fuel makes this a no-op -> stranded-outlet
// safe, lockstep.
inline void ApplyGraveyardExileAbility(GameState& state, int controller, int source_id, int mode)
{
    Permanent* src = nullptr;
    const CardDefinition* d = nullptr;
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller || p.tapped) { continue; }
        if (p.card.m_number != source_id) { continue; }
        const CardDefinition* pd = CardDatabase::Instance().LookupCached(p.card);
        if (!pd) { continue; }
        if (mode == 1 && pd->params.gy_exile_instant_sorcery_drain <= 0) { continue; }
        if (mode == 2 && pd->params.gy_exile_creature_lifegain <= 0) { continue; }
        if (!CanTapNow(p, state.battlefield)) { continue; }
        src = &p; d = pd;
        break;
    }
    if (!src || !d) { return; }
    std::vector<Card>& gy = state.players[controller].graveyard;
    for (std::size_t i = 0; i < gy.size(); ++i)
    {
        const Card& gc    = ZoneCard(gy[i]);   // graveyard cards are name-only placeholders
        const bool  match = (mode == 1) ? (gc.IsInstant() || gc.IsSorcery())
                                        : gc.IsCreature();
        if (!match) { continue; }
        gy.erase(gy.begin() + static_cast<std::ptrdiff_t>(i));
        src->tapped = true;   // the {T} part of the cost
        if (mode == 1)
        {
            state.players[1 - controller].life -= d->params.gy_exile_instant_sorcery_drain;
            state.opponent_lost_life_this_turn = true;
        }
        else
        {
            state.players[controller].life += d->params.gy_exile_creature_lifegain;
            state.players[controller].life_gained_this_turn += d->params.gy_exile_creature_lifegain;
        }
        return;
    }
}

// ---- Graveyard-return activated ability (Haven of the Spirit Dragon) -------------------------
// "{2}, {T}, Sacrifice this land: Return target Dragon creature card from your graveyard to your
// hand." One shared helper called IDENTICALLY from the executor (AIEngine) and the rollout
// (ApplyPlanDirect) so the zone changes stay lockstep.

// Is this graveyard card a legal target for `def`'s gy-return ability? Graveyard cards are
// name-only placeholders, so the caller must pass ZoneCard(...)-resolved cards (see ZoneCard).
inline bool GyReturnTargetLegal(const CardDefinition& def, const Card& gc)
{
    if (def.params.gy_return_requires_creature && !gc.IsCreature()) { return false; }
    if (!def.params.gy_return_requires_subtype.empty()
        && !CardHasSubtype(gc, def.params.gy_return_requires_subtype)) { return false; }
    return true;
}

// True if `controller` has at least one legal gy-return target for `def` right now.
inline bool GyReturnHasFuel(const GameState& state, int controller, const CardDefinition& def)
{
    for (const Card& c : state.players[controller].graveyard)
    { if (GyReturnTargetLegal(def, ZoneCard(c))) { return true; } }
    return false;
}

// Pay the {T} + Sacrifice half of the cost (the mana half is paid by the caller, like every other
// activated ability here) and return the chosen graveyard card to hand. `target_name` is the
// searched/human pick; an empty or now-illegal name resolves to the first legal target so a
// replayed line never strands. No-op if the source was tapped for mana by an earlier cast in the
// same plan, or if the fuel left the graveyard meanwhile -- stranded-outlet safe, like Deathrite.
inline void ApplyGraveyardReturnAbility(GameState& state, int controller, int source_id,
                                        const std::string& target_name)
{
    int src = -1;
    const CardDefinition* d = nullptr;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != controller || p.tapped) { continue; }
        if (p.card.m_number != source_id) { continue; }
        const CardDefinition* pd = CardDatabase::Instance().LookupCached(p.card);
        if (!pd || !pd->params.gy_return_cost.has_value()) { continue; }
        if (!CanTapNow(p, state.battlefield)) { continue; }
        src = i; d = pd;
        break;
    }
    if (src < 0 || !d) { return; }

    std::vector<Card>& gy = state.players[controller].graveyard;
    int pick = -1;
    for (std::size_t i = 0; i < gy.size(); ++i)
    {
        if (!GyReturnTargetLegal(*d, ZoneCard(gy[i]))) { continue; }
        if (pick < 0) { pick = static_cast<int>(i); }                 // fallback: first legal
        if (!target_name.empty() && gy[i].m_name == target_name) { pick = static_cast<int>(i); break; }
    }
    if (pick < 0) { return; }   // no legal target: the ability would not have been activated

    // Costs are paid on activation (CR 601.2h/602.1): tap, then sacrifice the source to the
    // graveyard, and only then resolve the return. The source is a Land, so it can never be its own
    // target even though it is in the graveyard by the time we move the picked card.
    // (`pick` stays valid: the sacrifice push_back only APPENDS to the graveyard.)
    const Card sacrificed = state.battlefield[src].card;
    state.battlefield.erase(state.battlefield.begin() + src);
    state.players[controller].graveyard.push_back(sacrificed);

    std::vector<Card>& gy2 = state.players[controller].graveyard;
    const Card returned = gy2[static_cast<std::size_t>(pick)];
    gy2.erase(gy2.begin() + pick);
    state.players[controller].hand.push_back(returned);
    if (g_play_event_sink && !g_tap_speculating)
    {
        EmitPlayEvent(state.turn_number, "ability",
                      sacrificed.m_name.str() + ": returned " + returned.m_name.str()
                      + " from the graveyard to hand");
    }
}

// Sacrifice-a-creature outlet (Skirk Prospector -> {R}; Siege-Gang -> 2 face damage; Pashalik -> two
// tokens). The mana cost is paid by the caller (subset math); here we SACRIFICE the chosen victim,
// apply the outlet payload from the source's params, and fire the victim's death-watchers.
// Canonical (most-expendable) sacrifice victim for a creature-sac outlet: prefer a TOKEN, else the
// lowest-power matching creature that is NOT the outlet source, else the source itself. Returns the
// victim's card.m_number, or -1 if none. MIRRORS the enumeration's bounded victim pick in
// TurnSolver::CollectActions -> the single-victim GT is unchanged and the multi-sac burst apply picks
// the same order in both worlds (executor + rollout), so it is lockstep. `need_sub` empty = any creature.
// `allow_enchantment` / `exclude_self` widen the victim filter for Slaughter-Priest of Mogis
// ("{2}, Sacrifice another creature OR AN ENCHANTMENT"): an enchantment permanent becomes legal
// fodder, and "another" makes the source itself ILLEGAL rather than merely last-ranked. Both
// default false -> every pre-existing outlet (Skirk / Siege-Gang / Pashalik) is byte-identical.
inline int CanonicalSacVictim(const GameState& state, int controller, int source_id,
                              const std::string& need_sub,
                              bool allow_enchantment = false, bool exclude_self = false)
{
    // Expendability heuristic (lower rank = sacrifice FIRST). Base metric is EFFECTIVE power (sac the
    // weakest), adjusted so we:
    //   - sac TOKENS and SELF-REPLACING bodies FIRST -- a Goblin token costs ~nothing, and Mogg War
    //     Marshal (dies -> creates a Goblin token) refills its own body, so saccing it is board-neutral;
    //   - DEFER (keep) SCALING creatures -- lords that buff/enable the rest of the board (Chieftain/King/
    //     Rundvelt +1/+1, Warchief cost-reduction) or payoffs that grow with it (Piledriver): losing one
    //     de-buffs every other Goblin, so they are sacked only when nothing else is left;
    //   - sac the outlet SOURCE last.
    // Matters most for the Skirk multi-sac burst, which sacrifices several victims in one turn.
    int victim_id = -1; int victim_rank = std::numeric_limits<int>::max();
    for (const Permanent& v : state.battlefield)
    {
        if (v.controller_index != controller) { continue; }
        const bool eligible = v.card.IsCreature()
                           || (allow_enchantment && v.card.IsEnchantment());
        if (!eligible) { continue; }
        if (exclude_self && v.card.m_number == source_id) { continue; }   // "another ..."
        // A subtype filter constrains only CREATURES; an enchantment admitted by
        // allow_enchantment is legal fodder on its type alone (the oracle says "or an
        // enchantment", not "or an enchantment of that type").
        if (!need_sub.empty() && v.card.IsCreature() && !CardHasSubtype(v.card, need_sub))
        { continue; }
        int rank = v.EffectivePower();                 // base: sac the weakest first
        const CardDefinition* d = CardDatabase::Instance().LookupCached(v.card);
        const bool self_replacing = d && d->params.dies_trigger_creates_tokens > 0
                                      && !d->params.dies_token_subtypes.empty();     // Mogg War Marshal
        const bool scaling = d && (
              (!d->params.subtypes_affected.empty()
               && (d->params.power_bonus > 0 || d->params.tough_bonus > 0
                   || !d->params.reduces_spell_subtype.empty()))                     // stat / tempo lord
              || d->params.attack_pump_power_per_other_matching > 0);                // Piledriver-style payoff
        if (v.is_token || self_replacing) { rank -= 1000; }    // tokens & Mogg: most expendable
        if (scaling)                      { rank += 1000; }    // lords / scaling payoffs: keep (defer)
        if (v.card.m_number == source_id) { rank += 100000; }  // sac the source last
        if (rank < victim_rank) { victim_rank = rank; victim_id = v.card.m_number; }
    }
    return victim_id;
}

// Sacrifice the permanent at battlefield index `idx`: to the graveyard, off the battlefield, then
// its death-watchers (Pashalik ping / Rundvelt impulse / Mogg death token). Factored out because
// the sac-outlet paths below each open-coded it, and the human-victim override needs to sacrifice
// by INDEX rather than by card number (see ChooseSacOutletVictimIndex).
inline void SacrificePermanentAt(GameState& state, int controller, int idx)
{
    if (idx < 0 || idx >= static_cast<int>(state.battlefield.size())) { return; }
    const Card dead     = state.battlefield[idx].card;
    const bool was_tok  = state.battlefield[idx].is_token;
    state.players[controller].graveyard.push_back(dead);
    state.battlefield.erase(state.battlefield.begin() + idx);
    FireSacrificeWatchers(state, controller);   // Slaughter-Priest ("whenever YOU sacrifice ...")
    OnCreatureDies(state, controller, dead, was_tok);
}

// Call of the Wild -- ONE activation: "Reveal the top card of your library. If it's a creature
// card, put it onto the battlefield. Otherwise, put it into your graveyard." The put creature
// enters through the full cascade (its own ETB fires; watchers fire). Cost paid by the caller
// (trailing-pass TapForCost, both worlds -> lockstep). Returns false when the library is empty.
inline bool ApplyRevealTopDeploy(GameState& state, int controller)
{
    Player& ap = state.players[controller];
    if (ap.library.empty()) { return false; }
    Card top = ap.library.DrawTop();
    const CardDefinition* d = CardDatabase::Instance().LookupCached(top);
    const bool is_creature = d ? d->card.IsCreature() : top.IsCreature();
    if (RevealVisible())
    {
        EmitReveal(state.turn_number, "Call of the Wild (reveal)",
                   { top.m_number }, { top.m_name.str() },
                   is_creature ? std::vector<int>{ top.m_number } : std::vector<int>{},
                   is_creature ? std::vector<int>{} : std::vector<int>{ top.m_number },
                   { is_creature ? "\xE2\x86\x92 battlefield" : "\xE2\x86\x92 graveyard" });
    }
    if (is_creature)
    {
        Permanent perm;
        perm.card              = d ? d->card : top;
        perm.card.m_number     = top.m_number;
        perm.controller_index  = controller;
        perm.owner_index       = controller;
        perm.entered_this_turn = true;
        state.battlefield.push_back(perm);
        const int slot = static_cast<int>(state.battlefield.size()) - 1;
        FireEtbWatchers(state, controller, slot);
        // kEtbKxHeuristic: a PUT Terastodon has no searched destroy-K axis -- the resolution-time
        // lethality heuristic picks one (HeuristicEtbDestroyK); inert for every other creature.
        FireOwnEtbTriggers(state, controller, slot, std::string(), kEtbKxHeuristic);
    }
    else
    {
        ap.graveyard.push_back(std::move(top));
    }
    return true;
}

// Turntimber Symbiosis front face: "Look at the top N cards of your library. You may put a
// creature card from among them onto the battlefield. If that card has mana value <= max_mv, it
// enters with +bonus +1/+1 counters. Put the rest on the bottom of your library in a random
// order" (deterministic bottom order -- unobservable in goldfish; Muxus precedent). WHICH
// creature = `chosen_name` (the searched tutor_target variant; every legal choice is a distinct
// plan variant, autonomous + human). Empty -> the provider-free default: highest-MV creature
// among the looked cards (disclosed); "TURNTIMBER_NONE" -> deliberately put nothing.
inline void PerformLookTopPutCreature(GameState& state, int controller, const CardParams& pp,
                                      const std::string& chosen_name,
                                      const std::string& source_name)
{
    Player& ap = state.players[controller];
    const int look = std::min(pp.look_top_put_creature_count,
                              static_cast<int>(ap.library.size()));
    if (look <= 0) { return; }
    std::vector<Card> looked;
    ap.library.DrawN(look, looked);

    int pick = -1;
    const bool decline = (chosen_name == "TURNTIMBER_NONE");
    if (!decline)
    {
        if (!chosen_name.empty())
        {
            for (int i = 0; i < look; ++i)
            {
                const CardDefinition* d = CardDatabase::Instance().LookupCached(looked[i]);
                const Card& c = d ? d->card : looked[i];
                if (c.IsCreature() && looked[i].m_name == chosen_name) { pick = i; break; }
            }
        }
        if (pick < 0)   // empty choice OR a named pick the post-shuffle top no longer holds
        {
            int best_mv = -1;
            for (int i = 0; i < look; ++i)
            {
                const CardDefinition* d = CardDatabase::Instance().LookupCached(looked[i]);
                const Card& c = d ? d->card : looked[i];
                if (!c.IsCreature()) { continue; }
                const int mv = c.m_mana_cost.ManaValue();
                if (mv > best_mv) { best_mv = mv; pick = i; }
            }
        }
        // Human-play put chooser (shared dig chooser; the Skyhunter attack-dig reuse precedent).
        // Human plans enumerate ONE empty-target cast for this spell (no clairvoyant named
        // variants -- see the collection site), so the human decides HERE, off the REAL look:
        // legal = every creature among the looked cards, reply = a looked index to put onto the
        // battlefield or -1 to put nothing; the heuristic default is the highest-MV pick above.
        // RevealLogPause nulls the chooser in every search/rollout/enumeration scope, so it
        // fires only on real resolution; autonomous play (named or empty target) is unchanged.
        if (g_play_dig_chooser && chosen_name.empty())
        {
            std::vector<int> legal;
            for (int i = 0; i < look; ++i)
            {
                const CardDefinition* d = CardDatabase::Instance().LookupCached(looked[i]);
                const Card& c = d ? d->card : looked[i];
                if (c.IsCreature()) { legal.push_back(i); }
            }
            if (!legal.empty())
            {
                const int c = (*g_play_dig_chooser)(state, controller, source_name + " (put)",
                                                    looked, legal, pick);
                pick = -1;
                for (int li : legal) { if (li == c) { pick = c; break; } }
            }
        }
    }

    if (RevealVisible())
    {
        std::vector<int> nums; std::vector<std::string> names; std::vector<std::string> labels;
        std::vector<int> kept, bottomed;
        for (int i = 0; i < look; ++i)
        {
            nums.push_back(looked[i].m_number); names.push_back(looked[i].m_name.str());
            if (i == pick) { kept.push_back(looked[i].m_number); labels.push_back("â battlefield"); }
            else           { bottomed.push_back(looked[i].m_number); labels.push_back("â bottom of library"); }
        }
        EmitReveal(state.turn_number, "Turntimber Symbiosis (look)", nums, names, kept, bottomed, labels);
    }

    for (int i = 0; i < look; ++i)
    {
        if (i == pick)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(looked[i]);
            Permanent perm;
            perm.card              = d ? d->card : looked[i];
            perm.card.m_number     = looked[i].m_number;
            perm.controller_index  = controller;
            perm.owner_index       = controller;
            perm.entered_this_turn = true;
            // "+1/+1 counters if mana value <= max_mv" -- counters ride the permanent (EffectivePower).
            const int mv = perm.card.m_mana_cost.ManaValue();
            if (pp.look_put_counter_bonus > 0 && mv <= pp.look_put_counter_bonus_max_mv)
            { perm.counters.push_back(Counter{ Counter::Type::PlusOnePlusOne, pp.look_put_counter_bonus }); }
            state.battlefield.push_back(perm);
            const int slot = static_cast<int>(state.battlefield.size()) - 1;
            FireEtbWatchers(state, controller, slot);
            // kEtbKxHeuristic: a PUT Terastodon has no searched destroy-K axis -- the
            // resolution-time lethality heuristic picks one; inert for every other creature.
            FireOwnEtbTriggers(state, controller, slot, std::string(), kEtbKxHeuristic);
        }
        else
        {
            ap.library.push_back(std::move(looked[i]));   // bottom, deterministic order
        }
    }
}

// Wirewood Lodge "{cost}, {T}: Untap target <subtype>". Preconditions (checked here so a
// stranded action is a FULL no-op -- cost unpaid by the caller when this returns false):
// the source exists, is controlled and untapped, and a TAPPED matching creature exists.
// Auto-target = the highest-yield tapped matching creature (mana-wise weakly dominant;
// disclosed). Taps the source, untaps the target. Lockstep both worlds.
inline bool CanApplyUntapCreature(const GameState& state, int controller, int source_id,
                                  const std::string& subtype)
{
    bool src_ok = false;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index == controller && p.card.m_number == source_id && !p.tapped)
        { src_ok = true; break; }
    }
    if (!src_ok) { return false; }
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index == controller && p.tapped && p.card.IsCreature()
            && CardHasSubtype(p.card, subtype)) { return true; }
    }
    return false;
}
inline void ApplyUntapCreature(GameState& state, int controller, int source_id,
                               const std::string& subtype)
{
    int src = -1, best = -1, best_yield = -1;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != controller) { continue; }
        if (p.card.m_number == source_id && !p.tapped) { src = i; continue; }
        if (!p.tapped || !p.card.IsCreature() || !CardHasSubtype(p.card, subtype)) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        const int y = d ? PermanentManaYield(state, p, *d) : 0;
        if (y > best_yield) { best_yield = y; best = i; }
    }
    if (src < 0 || best < 0) { return; }
    state.battlefield[src].tapped  = true;
    state.battlefield[best].tapped = false;
    if (g_play_event_sink && !g_tap_speculating)
    {
        EmitPlayEvent(state.turn_number, "ability",
                      state.battlefield[src].card.m_name.str() + ": untapped "
                      + state.battlefield[best].card.m_name.str());
    }
}

// ---- WHICH card a NON-CLEANUP discard sheds ----------------------------------------------------
// Two Minotaur effects discard OUTSIDE the cleanup step: Burning-Fist Minotaur's "{1}{R}, Discard a
// card:" activation cost and Neheb, the Worthy's combat-damage trigger. Both are real player
// decisions, so both route through the SAME `discard` decision type the cleanup shed already uses
// (g_play_discard_chooser) rather than silently taking the heuristic -- the 2c-ter bucket-A case
// (reuse an existing decision type; nothing new to build).
//
// Autonomously the pick is the provider's cleanup-discard ranking, i.e. the deck's own authored
// discard doctrine, applied to a cost instead of a hand-size shed. The chooser is nulled by
// RevealLogPause in every search/rollout scope, so the search is byte-identical and the human only
// sees the decision on a REAL resolution. Returns a hand index, or -1 when the hand is empty.
// Set while a COST/TRIGGER discard decision is being surfaced (the ability's source name), so the
// decision writer can frame it correctly. The cleanup-step shed leaves it empty; without this the
// shared `discard` decision rendered with the cleanup framing and a 2-card hand read
// "Select -5 cards to discard" (Minotaur seed 1, Burning-Fist activation).
inline std::string& NonCleanupDiscardContext()
{
    static thread_local std::string v;
    return v;
}

inline int ChooseNonCleanupDiscardIndex(const GameState& state, int controller,
                                        const std::string& source_name)
{
    const Player& ap = state.players[controller];
    if (ap.hand.empty()) { return -1; }
    const std::vector<int> rank = ResolveProvider(state).CleanupDiscardCandidates(state, nullptr);
    const int hn = static_cast<int>(ap.hand.size());
    int pick = (!rank.empty() && rank.front() >= 0 && rank.front() < hn) ? rank.front() : 0;

    // MTG_NONCLEANUP_SHED_WORST=1: TESTING-ONLY anti-heuristic, the MTG_SHED_WORST tradition --
    // take the LAST-ranked eligible candidate instead of the first. Paired against the default it
    // BRACKETS this entire axis: no ranking can be worth more than best-vs-worst, so a zero delta
    // proves the site cannot pay and closes it without building a searched axis for it.
    //
    // This site needs its own bound because MTG_SHED_WORST reaches only the ROLLOUT's cleanup
    // (CleanupDiscardShedSet), and the decks that discard here may never reach a cleanup at all --
    // Minotaur reaches ChooseDiscard zero times while consulting this function 118 times per 200
    // games (Burning-Fist's activation cost, Neheb's combat-damage trigger). See
    // docs/design/per-deck-discard-analysis-phase.md. Never set in play.
    //
    // Staged entries are skipped: the ranking's last-resort tier keeps them, but a staged card is
    // in EXILE and cannot be shed, so rank.back() is not necessarily a legal victim.
    static const bool s_worst_env = EnvOn("MTG_NONCLEANUP_SHED_WORST");
    if (heurarm::Flag(heurarm::NONCLEANUP_SHED_WORST, s_worst_env))
    {
        for (auto it = rank.rbegin(); it != rank.rend(); ++it)
        {
            if (*it < 0 || *it >= hn) { continue; }
            if (ap.hand[static_cast<std::size_t>(*it)].m_is_staged) { continue; }
            pick = *it;
            break;
        }
    }
    if (g_play_discard_chooser)
    {
        std::vector<int> idxs(ap.hand.size());
        for (int i = 0; i < static_cast<int>(ap.hand.size()); ++i) { idxs[i] = i; }
        NonCleanupDiscardContext() = source_name;
        const int chosen = (*g_play_discard_chooser)(state, controller, idxs, pick);
        NonCleanupDiscardContext().clear();
        if (chosen >= 0 && chosen < static_cast<int>(ap.hand.size())) { pick = chosen; }
    }
    return pick;
}

// ---- Main-phase ACTIVATED PUMP (Action::Kind::ActivatePump) -------------------------------------
// One shared apply for both shapes, called IDENTICALLY from the rollout (TurnSolver's trailing
// apply pass) and the executor (AIEngine::TakeTurn) so the two worlds stay lockstep.
//
//   mode 1 -- SELF pump with a discard rider (Burning-Fist Minotaur). Per activation: the source
//             gains +firebreathing_power/+0 until end of turn and its controller discards ONE card,
//             chosen by the provider's cleanup-discard ranking (so a deck that owns its discard
//             doctrine sheds by that doctrine here too, not by an ad-hoc rule). Stops early when
//             the hand runs out -- "Discard a card" is an unpayable cost with an empty hand.
//   mode 2 -- TEAM pump + HASTE (Sethron). Per activation: every creature the controller controls
//             matching team_pump_subtypes gets +team_pump_power/+0 and temp_haste until end of
//             turn. Menace is inert (no blockers) and not modelled -- disclosed deferral D7.
//
// The MANA has already been paid by the caller (pre-scaled to K x unit cost), so this only applies
// the effects; `k` is the activation count. Returns the number of activations actually realised
// (mode 1 can stop short on an empty hand), which is what the caller logs.
inline int ApplyActivatePump(GameState& state, int controller, int source_id, int mode, int k)
{
    int src = -1;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index == controller && p.card.m_number == source_id) { src = i; break; }
    }
    if (src < 0) { return 0; }   // source left the battlefield: stranded-outlet safe, a full no-op
    const CardDefinition* sd = CardDatabase::Instance().LookupCached(state.battlefield[src].card);
    if (!sd) { return 0; }
    int done = 0;
    for (int n = 0; n < std::max(1, k); ++n)
    {
        if (mode == 1)
        {
            Player& ap = state.players[controller];
            if (ap.hand.empty()) { break; }                       // unpayable additional cost
            const int hi = ChooseNonCleanupDiscardIndex(state, controller,    // surfaced as `discard`
                                                        state.battlefield[src].card.m_name.str());
            if (hi < 0) { break; }
            const std::string shed = ap.hand[static_cast<std::size_t>(hi)].m_name.str();
            ap.graveyard.push_back(ap.hand[static_cast<std::size_t>(hi)]);
            ap.hand.erase(ap.hand.begin() + hi);
            state.battlefield[src].temp_power_bonus += sd->params.firebreathing_power;
            if (g_play_event_sink && !g_tap_speculating)
            {
                EmitPlayEvent(state.turn_number, "ability",
                              state.battlefield[src].card.m_name.str() + ": +"
                              + std::to_string(sd->params.firebreathing_power)
                              + "/+0 (discarded " + shed + ")");
            }
        }
        else   // mode 2 -- team pump + haste
        {
            for (Permanent& q : state.battlefield)
            {
                if (q.controller_index != controller) { continue; }
                if (!q.card.IsCreature() && !q.is_animated) { continue; }
                bool m = sd->params.team_pump_subtypes.empty();
                for (const std::string& sub : sd->params.team_pump_subtypes)
                { if (q.is_animated || CardHasSubtype(q.card, sub)) { m = true; break; } }
                if (!m) { continue; }
                q.temp_power_bonus += sd->params.team_pump_power;
                if (sd->params.team_pump_grants_haste) { q.temp_haste = true; }
            }
            if (g_play_event_sink && !g_tap_speculating)
            {
                EmitPlayEvent(state.turn_number, "ability",
                              state.battlefield[src].card.m_name.str() + ": Minotaurs +"
                              + std::to_string(sd->params.team_pump_power) + "/+0 and haste");
            }
        }
        ++done;
    }
    return done;
}

// HUMAN-PLAY victim override for a creature-sac outlet (viewer issue #4). Autonomously the victim is
// CanonicalSacVictim's expendability pick and the human never saw it -- but "which Goblin dies" is a
// real decision (feeding a lord to Skirk de-buffs the whole board; feeding Mogg War Marshal is nearly
// free; feeding Rundvelt Hordemaster is how you buy its impulse dig). Reuses the existing `sacrifice`
// board-click decision (g_play_sacrifice_chooser), so no new decision type is introduced.
//
// Returns a BATTLEFIELD INDEX, or -1 when there is no chooser (autonomous / search / rollout -- the
// pointer is nulled by RevealLogPause) so the caller keeps its byte-identical heuristic path. Indices
// rather than card numbers deliberately: TOKENS all carry m_number 0, so the card-number lookup the
// heuristic path uses cannot distinguish two different tokens -- fine when they are fungible, not
// fine when a human is pointing at one.
inline int ChooseSacOutletVictimIndex(GameState& state, int controller, int source_id,
                                      const std::string& need_sub, int heuristic_vid,
                                      const std::string& source_name)
{
    if (!g_play_sacrifice_chooser) { return -1; }
    std::vector<int> cands;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& v = state.battlefield[i];
        if (v.controller_index != controller || !v.card.IsCreature()) { continue; }
        if (!need_sub.empty() && !CardHasSubtype(v.card, need_sub)) { continue; }
        cands.push_back(i);
    }
    if (cands.empty())     { return -1; }
    if (cands.size() == 1) { return cands[0]; }   // forced -- do not prompt for a non-choice
    int def_opt = 0;
    for (int k = 0; k < static_cast<int>(cands.size()); ++k)
    { if (state.battlefield[cands[k]].card.m_number == heuristic_vid) { def_opt = k; break; } }
    const int chosen = (*g_play_sacrifice_chooser)(state, controller, source_name, cands, def_opt);
    if (chosen >= 0 && chosen < static_cast<int>(cands.size())) { return cands[chosen]; }
    return cands[def_opt];
    (void)source_id;
}

inline void ApplySacCreatureOutlet(GameState& state, int controller, int source_id, int victim_id)
{
    const CardParams* op = nullptr;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index == controller && p.card.m_number == source_id)
        {
            const CardDefinition* od = CardDatabase::Instance().LookupCached(p.card);
            if (od && od->params.sac_creature_outlet) { op = &od->params; }
            break;
        }
    }
    if (!op) { return; }
    // Human play re-asks WHICH creature dies against the real resolution board (viewer issue #4);
    // with no chooser this is -1 and the card-number search below runs byte-identically.
    std::string src_name;
    for (const Permanent& p : state.battlefield)
    { if (p.controller_index == controller && p.card.m_number == source_id)
      { src_name = p.card.m_name.str(); break; } }
    const int hidx = ChooseSacOutletVictimIndex(state, controller, source_id,
                                                op->sac_creature_requires_subtype, victim_id, src_name);
    // Find + remove the chosen victim (a controlled creature of the required subtype; self-inclusive).
    Card victim; bool found = false; bool victim_tok = false;
    if (hidx >= 0)
    {
        victim = state.battlefield[hidx].card; found = true;
        victim_tok = state.battlefield[hidx].is_token;
        state.players[controller].graveyard.push_back(victim);
        state.battlefield.erase(state.battlefield.begin() + hidx);
    }
    // The baked victim id may be STALE: two outlets (or two activations) enumerated against the
    // same board bake the same canonical victim, and the first apply consumes it -- with tokens
    // now uniquely numbered the second activation's id scan then misses (under shared id 0 it
    // silently aliased onto the next token, which is why this never surfaced before numbering).
    // TOKEN victims are fungible, so on a miss RE-PICK canonically against the live board.
    //
    // TOKENS ONLY (id >= 1000, or legacy 0). A missing REAL-card victim means the PLAN'S PREMISE
    // failed -- the search baked a victim its own line imagined into existence (goblins gi123:
    // the search's combat Lackey-put a STAGED Mogg War Marshal that reality never puts, then
    // sacked it; re-picking substituted the live Goblin Lackey, converting a harmless stale plan
    // into the loss of a real attacker and a full turn). Fungibility justifies substitution only
    // among same-premise token victims; for a real card the sac must NO-OP, exactly as it did
    // before numbering. Shared by executor + rollout -> lockstep.
    const bool victim_was_token = (victim_id == 0 || victim_id >= 1000);
    for (int attempt = 0; !found && attempt < 2; ++attempt)
    {
        for (int i = 0; !found && i < static_cast<int>(state.battlefield.size()); ++i)
        {
            Permanent& q = state.battlefield[i];
            if (q.controller_index != controller) { continue; }
            // Slaughter-Priest widening: an ENCHANTMENT is legal fodder too, and "another" bars
            // the source itself. Both gated -> every pre-existing outlet keeps the creature-only,
            // source-allowed filter exactly (byte-identical).
            if (!q.card.IsCreature()
                && !(op->sac_outlet_allows_enchantment && q.card.IsEnchantment())) { continue; }
            if (q.card.m_number != victim_id) { continue; }
            if (op->sac_outlet_excludes_self && q.card.m_number == source_id) { continue; }
            if (!op->sac_creature_requires_subtype.empty() && q.card.IsCreature()
                && !CardHasSubtype(q.card, op->sac_creature_requires_subtype)) { continue; }
            victim = q.card; found = true; victim_tok = q.is_token;
            state.players[controller].graveyard.push_back(q.card);
            state.battlefield.erase(state.battlefield.begin() + i);
            break;
        }
        if (found || attempt > 0 || !victim_was_token) { break; }
        victim_id = CanonicalSacVictim(state, controller, source_id,
                                       op->sac_creature_requires_subtype,
                                       op->sac_outlet_allows_enchantment,
                                       op->sac_outlet_excludes_self);
        if (victim_id < 0) { break; }
    }
    if (!found) { return; }
    // Payload (copy the scalars before CreateToken invalidates `op` via battlefield realloc).
    const std::string mana_color = op->sac_outlet_add_mana_color;
    const int mana_amt = op->sac_outlet_add_mana_amount, dmg = op->sac_outlet_damage;
    const int ntok = op->sac_outlet_creates_tokens, tp = op->sac_outlet_token_power,
              tt = op->sac_outlet_token_toughness;
    const std::vector<std::string> tsub = op->sac_outlet_token_subtypes;
    if (!mana_color.empty()) { AddChosenColorFloat(state, mana_color, mana_amt); }
    if (dmg > 0) { state.players[1 - controller].life -= dmg; state.opponent_lost_life_this_turn = true; }
    for (int k = 0; k < ntok; ++k) { CreateToken(state, controller, tp, tt, tsub); }
    FireSacrificeWatchers(state, controller);   // Slaughter-Priest ("whenever YOU sacrifice ...")
    OnCreatureDies(state, controller, victim, victim_tok);   // Pashalik ping / Rundvelt impulse / Mogg death token
}

// Multi-sac BURST: sacrifice up to `count` canonical victims to a damage sac-outlet in ONE activation
// (Siege-Gang saccing the swarm for count*damage burst lethal). Loops the single-victim apply, picking
// the most-expendable matching victim each time (the board shrinks, so no victim is picked twice). The
// activation MANA cost (count * per-sac cost) is paid by the caller's TapForCost, exactly like the
// single action -- if unaffordable the caller does not call this, so a stranded burst is a no-op. Shared
// by the executor (AIEngine) and the rollout (TurnSolver) -> lockstep. Inert unless a burst action is
// emitted (only damage outlets with >=2 victims), so every other deck is byte-identical.
inline void ApplySacCreatureOutletBurst(GameState& state, int controller, int source_id, int count)
{
    std::string need_sub;
    bool allow_ench = false, excl_self = false;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index == controller && p.card.m_number == source_id)
        {
            const CardDefinition* od = CardDatabase::Instance().LookupCached(p.card);
            if (od)
            {
                need_sub   = od->params.sac_creature_requires_subtype;
                allow_ench = od->params.sac_outlet_allows_enchantment;
                excl_self  = od->params.sac_outlet_excludes_self;
            }
            break;
        }
    }
    for (int n = 0; n < count; ++n)
    {
        int victim_id = CanonicalSacVictim(state, controller, source_id, need_sub,
                                           allow_ench, excl_self);
        if (victim_id < 0) { break; }   // ran out of victims
        ApplySacCreatureOutlet(state, controller, source_id, victim_id);
    }
}

// Twinshot Sniper "Channel -- {1}{R}, Discard this card: 2 damage to any target." From HAND: discard
// the card (mana cost paid by the caller) -> channel_damage to the opponent face. Lockstep.
inline void ApplyChannel(GameState& state, int controller, int hand_index,
                         const std::string& card_name, int damage)
{
    Player& ap = state.players[controller];
    int idx = -1;
    if (hand_index >= 0 && hand_index < static_cast<int>(ap.hand.size())
        && ap.hand[hand_index].m_name == card_name) { idx = hand_index; }
    else { for (int i = 0; i < static_cast<int>(ap.hand.size()); ++i)
           { if (ap.hand[i].m_name == card_name) { idx = i; break; } } }
    if (idx < 0) { return; }
    ap.graveyard.push_back(ap.hand[idx]);
    ap.hand.erase(ap.hand.begin() + idx);
    state.players[1 - controller].life -= damage;
    state.opponent_lost_life_this_turn = true;
}

// Utvara Hellkite: "Whenever a Dragon you control attacks, create a 6/6 Dragon token." Per
// ATTACKING matching creature. Called at declare-attackers with the finalized `attackers`. Tokens
// enter UNTAPPED + summoning-sick via CreateToken (so each fires FireEtbWatchers: Scourge ping /
// Lathliss token) and are NOT returned to this combat. Counts are gathered BEFORE any CreateToken
// (which invalidates the `attackers` pointers). Distinct from the flat tapped-and-attacking
// FireAttackCreateTokens (Adeline).
inline void FireUtvaraAttackTokens(GameState& state, int controller,
                                   const std::vector<const Permanent*>& attackers)
{
    if (attackers.empty()) { return; }
    struct Spec { int n, p, t; std::vector<std::string> subs; std::vector<std::string> kws; };
    std::vector<Spec> specs;
    const int bf_size = static_cast<int>(state.battlefield.size());
    for (int i = 0; i < bf_size; ++i)
    {
        const Permanent& src = state.battlefield[i];
        if (src.controller_index != controller) { continue; }
        const CardDefinition* sdef = CardDatabase::Instance().LookupCached(src.card);
        if (!sdef || sdef->params.attack_per_matching_creates_tokens <= 0) { continue; }
        int matching = 0;
        for (const Permanent* atk : attackers)
        {
            if (sdef->params.attack_token_requires_subtypes.empty()) { ++matching; continue; }
            bool m = atk->is_animated;   // animated land = every creature type
            for (const std::string& req : sdef->params.attack_token_requires_subtypes)
            {
                if (m) { break; }
                if (CardHasSubtype(atk->card, req)) { m = true; }
            }
            if (m) { ++matching; }
        }
        if (matching <= 0) { continue; }
        specs.push_back({ sdef->params.attack_per_matching_creates_tokens * matching,
                          sdef->params.attack_per_token_power,
                          sdef->params.attack_per_token_toughness,
                          sdef->params.attack_per_token_subtypes,
                          sdef->params.attack_per_token_keywords });
    }
    for (const Spec& s : specs)
    {
        for (int k = 0; k < s.n; ++k)
        {
            // untapped; pings via FireEtbWatchers (and, with a Dragon Tempest out, gains haste)
            CreateToken(state, controller, s.p, s.t, s.subs, std::string(), s.kws);
        }
    }
}

// ---- Goblins combat attack-trigger self-pumps (Piledriver / Muxus) -------------------------------
// "Whenever this attacks, it gets +X/+Y until end of turn for each other <...>." Applied at
// declare-attackers in BOTH worlds (executor CombatPhase + rollout SimulateCombat), writing
// temp_power_bonus / temp_tough_bonus on the matching attacker so EffectivePower()/EffectiveToughness()
// pick it up in the damage loop. `attacker_indices` are stable battlefield indices of the finalized
// declared attackers (after any attack-trigger token creation). Two flavours, keyed by param:
//   * Goblin Piledriver -- attack_pump_power_per_other_matching over subtypes_affected: +power per
//     OTHER attacking creature whose subtype matches (self-excluded). Mirrors CountAttackTriggerLifeLoss.
//   * Muxus -- attack_self_pump_per_other_subtype/_power/_tough: base is other CONTROLLED permanents
//     (not just attackers) whose subtype matches (self-excluded).
// Gated: an attacker whose def sets neither param is untouched -> every other deck byte-identical.
inline void ApplyAttackSelfPumps(GameState& state, int controller,
                                 const std::vector<int>& attacker_indices)
{
    if (attacker_indices.empty()) { return; }
    const int bf_size = static_cast<int>(state.battlefield.size());
    for (int idx : attacker_indices)
    {
        if (idx < 0 || idx >= bf_size) { continue; }
        Permanent& self = state.battlefield[idx];
        if (self.controller_index != controller) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(self.card);
        if (!d) { continue; }
        const CardParams& p = d->params;

        // Piledriver: +power per OTHER attacking creature whose subtype is in subtypes_affected.
        if (p.attack_pump_power_per_other_matching > 0 && !p.subtypes_affected.empty())
        {
            int others = 0;
            for (int oidx : attacker_indices)
            {
                if (oidx == idx || oidx < 0 || oidx >= bf_size) { continue; }   // "each OTHER"
                const Permanent& other = state.battlefield[oidx];
                bool m = other.is_animated;   // animated land = every creature type
                for (const std::string& sub : p.subtypes_affected)
                {
                    if (m) { break; }
                    if (CardHasSubtype(other.card, sub)) { m = true; }
                }
                if (m) { ++others; }
            }
            self.temp_power_bonus += p.attack_pump_power_per_other_matching * others;
        }

        // Muxus: +power/+tough per OTHER permanent you control whose subtype matches.
        if (!p.attack_self_pump_per_other_subtype.empty()
            && (p.attack_self_pump_power != 0 || p.attack_self_pump_tough != 0))
        {
            int others = 0;
            for (int j = 0; j < bf_size; ++j)
            {
                if (j == idx) { continue; }                            // "each OTHER"
                const Permanent& q = state.battlefield[j];
                if (q.controller_index != controller) { continue; }
                if (q.is_animated
                    || CardHasSubtype(q.card, p.attack_self_pump_per_other_subtype))
                { ++others; }
            }
            self.temp_power_bonus += p.attack_self_pump_power * others;
            self.temp_tough_bonus += p.attack_self_pump_tough * others;
        }
    }

    // Kragma Warcaller: "Whenever a Minotaur you control attacks, it gets +2/+0 until end of turn."
    // A TEAM attack trigger, so it is driven by the SOURCES the controller has in play rather than
    // by each attacker's own params (the two loops above). One trigger per (source, matching
    // attacker) pair, and the source pumps ITSELF when it attacks -- the oracle says "a Minotaur
    // you control", not "another". The source need not be attacking for the trigger to fire on
    // other attackers. Gated on the param, so the source scan is skipped for every other deck.
    {
        const int bf_all = static_cast<int>(state.battlefield.size());
        for (int si = 0; si < bf_all; ++si)
        {
            const Permanent& src = state.battlefield[si];
            if (src.controller_index != controller) { continue; }
            const CardDefinition* sd = CardDatabase::Instance().LookupCached(src.card);
            if (!sd || sd->params.attack_pump_matching_power <= 0) { continue; }
            for (int idx : attacker_indices)
            {
                if (idx < 0 || idx >= bf_all) { continue; }
                Permanent& a = state.battlefield[idx];
                if (a.controller_index != controller) { continue; }
                bool m = sd->params.subtypes_affected.empty();   // empty = every attacker
                for (const std::string& sub : sd->params.subtypes_affected)
                { if (a.is_animated || CardHasSubtype(a.card, sub)) { m = true; break; } }
                if (m) { a.temp_power_bonus += sd->params.attack_pump_matching_power; }
            }
        }
    }
}

// ---- Two-Headed Hellkite: "Whenever this creature attacks, draw two cards." ---------------------
// Self-only attack-trigger draw (CardParams::attack_draw_cards), applied once per attacking copy at
// declare-attackers alongside ApplyAttackSelfPumps, in BOTH worlds (GameEngine::CombatPhase executor
// + TurnSolver::SimulateCombat rollout) so search and execution agree on the drawn resources.
// Drawing from an empty library loses the game -- same player_lost_on_draw flag as the draw step.
// Gated: an attacker whose def leaves attack_draw_cards at 0 is untouched -> other decks
// byte-identical.
inline void ApplyAttackDrawTriggers(GameState& state, int controller,
                                    const std::vector<int>& attacker_indices)
{
    if (attacker_indices.empty()) { return; }
    const int bf_size = static_cast<int>(state.battlefield.size());
    Player& pl = state.players[controller];
    for (int idx : attacker_indices)
    {
        if (idx < 0 || idx >= bf_size) { continue; }
        const Permanent& self = state.battlefield[idx];
        if (self.controller_index != controller) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(self.card);
        if (!d || d->params.attack_draw_cards <= 0) { continue; }
        for (int k = 0; k < d->params.attack_draw_cards; ++k)
        {
            if (pl.library.empty()) { state.player_lost_on_draw = true; return; }
            std::size_t before = pl.hand.size();
            pl.library.DrawN(1, pl.hand);
            pl.cards_drawn_this_turn += static_cast<int>(pl.hand.size() - before);
            if (g_play_draw_sink)
            {
                for (std::size_t hi = before; hi < pl.hand.size(); ++hi)
                { g_play_draw_sink->push_back({ state.turn_number, pl.hand[hi].m_name.str() }); }
            }
        }
    }
}

// ---- Goblin Lackey: cheat a Goblin permanent into play on combat damage --------------------------
// "Whenever this creature deals combat damage to a player, you may put a Goblin permanent card from
// your hand onto the battlefield." Fired at the combat-damage step (both worlds) for each of the
// controller's attackers that (a) has combat_damage_puts_subtype_from_hand non-empty and (b) dealt
// positive combat damage (its index is in `damaging_attacker_indices`). For each, deterministically
// cheat the HIGHEST-impact matching permanent from hand into play through the shared enter cascade
// (FireEtbWatchers + FireOwnEtbTriggers -- mirroring PerformMuxusReveal), so the cheated body fires its own
// ETB (Siege-Gang tokens, Muxus reveal, ...). Heuristic pick: highest mana value, tie-break higher
// printed power then lower card number (deterministic; the human-play/viewer chooser is wired later).
// Gated: no Lackey-flagged damaging attacker (or no matching hand card) -> no-op, other decks identical.
// Forward declaration: EnforceLegendRule is defined later in this header (the cheat-into-play put
// must run it immediately -- CR 704.5j is a state-based action; see the call site below).
inline void EnforceLegendRule(GameState& state, int controller_index);

// Forward declaration only (ai/ManaPayment.h cannot be included here -- it includes this header).
// Used solely by the MTG_LACKEY_PREF diagnostic below, never by game logic. MUST match the real
// signature in ManaPayment.h, default argument included, or every call site becomes ambiguous.
ManaPool AvailableManaPool(const GameState& state, const Permanent* skip);

// fwd decl (defined further down; used by ProjectEtbDestroyK's Natural Order route check)
inline std::vector<int> SacCreatureCandidateIndices(const GameState& state, int controller,
                                                    const std::string& color);

// ---- Terastodon destroy-K projection (USER doctrine 2026-08-20/21) -----------------------------
// ONE projected K -- no searched fan ("I don't want to roll them all out. That's too expensive.
// It would be better to make a projection") -- decided at RESOLUTION for BOTH entry paths: the
// autonomous cast rides the same kEtbKxHeuristic sentinel as the puts. Resolving mid-plan is the
// practical form of "we might be able to see it in the current turn plan? ... That would make
// things the easiest": casts resolved before Terastodon are already on the battlefield, and the
// REMAINING pool prices what the plan can still do today, so the this-turn Craterhoof case needs
// only a route-in-reach test plus the lethality check ("we would need to calculate for the
// current turn that the Craterhoof play is lethal, but nothing else").
//
// The principle (USER): "all we need to do is avoid a case where we miscalculate the turn we can
// win on. If we don't mess that up then we could just figure out what is required in terms of
// elephants to end the game on that turn." Earliest winnable horizon, then the required K:
//   h=0  -- a Craterhoof can still land TODAY (in hand; a live Call of the Wild flipping it off
//           the clairvoyant top, or after a Worldly Tutor; a Natural Order fetch -- USER: "all
//           valid ways") with the remaining pool covering the route: smallest K whose swing is
//           lethal this turn ("maybe playing maximum elephants is not necessary"). The fresh
//           Elephants and Terastodon are summoning-sick and only feed the Hoof's X. Skipped when
//           a Hoof is ALREADY down (its X is locked; new Elephants no longer pump anything).
//   h=1  -- smallest K whose plain next-turn swing is lethal (everything untaps; an AFFORDABLE
//           power-4+ hand threat's power folds in, which is how "keep the permanents and
//           develop" falls out as K=0 automatically).
//   h=1' -- ONLY when h=1 fails ("only for the next if our projection would go to the turn after
//           otherwise"): a Craterhoof dropped NEXT turn -- the trickier mana check: every board
//           source's yield regardless of tapped-ness, minus the K eaten sources, plus a land
//           drop if a land is in hand.
//   h=2  -- smallest K lethal over two swings.
//   none -- cap: the Elephants are the only clock ("go for broke").
// Projections use EffectivePower -- net of until-EOT bonuses for the future horizons (a resolved
// pump must not inflate next turn's swing) -- and no lord bonuses (under-counting only ever asks
// for MORE elephants). The plan carrying the pick is still scored by its rollout, and human play
// / MTG_UNPRUNED(terak) keep the explicit 0..cap fan at the cast site.
inline int ProjectEtbDestroyK(const GameState& state, int controller, const CardDefinition& def)
{
    const int kcap = std::min(def.params.etb_destroy_own_noncreature_max,
                              CountEtbDestroyTargets(state, controller));
    if (kcap <= 0) { return 0; }
    const Player& ap     = state.players[controller];
    const int opp_life   = state.players[1 - controller].life;
    const int pool_total = AvailableManaPool(state, nullptr).Total();   // remaining, mid-plan
    const int per        = std::max(1, def.params.etb_created_token_power);   // 3/3 Elephants

    // Board census (the entered Terastodon is already on the battlefield and counts itself):
    // creatures (X fodder), this-turn attackers, and next-turn power net of until-EOT bonuses.
    int n_cre = 0, n_att = 0, att_power = 0, base_power = 0;
    bool hoof_down = false;
    for (const Permanent& q : state.battlefield)
    {
        if (q.controller_index != controller) { continue; }
        const CardDefinition* qd = CardDatabase::Instance().LookupCached(q.card);
        if (qd && qd->params.etb_team_pump_per_creature) { hoof_down = true; }
        if (!(q.card.IsCreature() || q.is_animated)) { continue; }
        ++n_cre;
        base_power += std::max(0, q.EffectivePower() - q.temp_power_bonus);
        if (CanAttackFull(q, state.battlefield, controller))
        { ++n_att; att_power += std::max(0, q.EffectivePower()); }
    }

    // Hoof routes still in reach: cheapest extra mana to land a team-pump haste finisher this
    // turn (`route`, priced against the REMAINING pool) / next turn (`nroute`, priced against
    // next turn's full untap below). A Natural Order route docks one creature for its sacrifice.
    const CardDefinition* hoof = nullptr;  int route = -1;  bool route_no = false;
    const CardDefinition* nhoof = nullptr; int nroute = -1; bool nroute_no = false;
    {
        auto consider = [&](const CardDefinition* h, int cost, bool is_no, bool this_turn_too)
        {
            if (this_turn_too && (route < 0 || cost < route))
            { hoof = h; route = cost; route_no = is_no; }
            if (nroute < 0 || cost < nroute) { nhoof = h; nroute = cost; nroute_no = is_no; }
        };
        const CardDefinition* hand_hoof = nullptr;
        for (const Card& hc : ap.hand)
        {
            const CardDefinition* hd = CardDatabase::Instance().LookupCached(hc);
            if (hd && hd->params.etb_team_pump_per_creature) { hand_hoof = hd; break; }
        }
        if (hand_hoof != nullptr)
        { consider(hand_hoof, hand_hoof->card.m_mana_cost.ManaValue(), false, true); }
        else
        {
            const CardDefinition* lib_hoof = nullptr;
            for (const Card& lc : ap.library)
            {
                const CardDefinition* ld = CardDatabase::Instance().LookupCached(lc);
                if (ld && ld->params.etb_team_pump_per_creature) { lib_hoof = ld; break; }
            }
            if (lib_hoof != nullptr)
            {
                for (const Permanent& q : state.battlefield)   // a live Call of the Wild
                {
                    if (q.controller_index != controller || q.tapped) { continue; }
                    const CardDefinition* qd = CardDatabase::Instance().LookupCached(q.card);
                    if (!qd || !qd->params.activated_reveal_top_cost.has_value()) { continue; }
                    const int cotw = qd->params.activated_reveal_top_cost->ManaValue();
                    if (!ap.library.empty())
                    {
                        const CardDefinition* td =
                            CardDatabase::Instance().LookupCached(ap.library[0]);
                        // Top already the Hoof: flip alone -- THIS turn only (next turn's draw
                        // moves the top out from under the projection).
                        if (td && td->params.etb_team_pump_per_creature)
                        { consider(lib_hoof, cotw, false, true); }
                    }
                    for (const Card& hc : ap.hand)
                    {
                        const CardDefinition* hd = CardDatabase::Instance().LookupCached(hc);
                        if (hd && hd->params.tutor_to_top)
                        { consider(lib_hoof, cotw + hd->card.m_mana_cost.ManaValue(), false, true); }
                    }
                    break;   // one Call of the Wild is enough for the route census
                }
                for (const Card& hc : ap.hand)   // Natural Order fetching it straight in
                {
                    const CardDefinition* hd = CardDatabase::Instance().LookupCached(hc);
                    if (!hd || hd->params.sac_additional_creature_color.empty()
                        || !hd->params.tutor_to_battlefield_single) { continue; }
                    if (SacCreatureCandidateIndices(state, controller,
                            hd->params.sac_additional_creature_color).empty()) { continue; }
                    consider(lib_hoof, hd->card.m_mana_cost.ManaValue(), true, true);
                    break;
                }
            }
        }
    }

    // h=0: a Hoof still lands today -> smallest K whose swing is lethal THIS turn.
    if (!hoof_down && hoof != nullptr && route >= 0 && pool_total >= route)
    {
        int xa = n_att, xp = att_power, xc = n_cre;
        if (route_no) { xc -= 1; if (xa > 0) { --xa; xp = std::max(0, xp - 1); } }
        const int hoofp = hoof->card.m_power.value_or(0);
        for (int k = 0; k <= kcap; ++k)
        {
            const int x      = xc + k + 1;                       // + Elephants + the Hoof itself
            const int attack = xp + hoofp + (xa + 1) * x;
            if (attack >= opp_life) { return k; }
        }
    }

    // The best AFFORDABLE additional threat's power, folded into the plain h=1/h=2 swings
    // ("no elephants so we can afford other threats" now falls out of the arithmetic).
    int extra_p = 0;
    for (const Card& hc : ap.hand)
    {
        const CardDefinition* hd = CardDatabase::Instance().LookupCached(hc);
        if (hd && hd->card.IsCreature() && hd->card.m_power.value_or(0) >= 4
            && pool_total >= hd->card.m_mana_cost.ManaValue())
        { extra_p = std::max(extra_p, hd->card.m_power.value_or(0)); }
    }

    // h=1: smallest K whose plain next-turn swing is lethal.
    for (int k = 0; k <= kcap; ++k)
    { if (base_power + extra_p + per * k >= opp_life) { return k; } }

    // h=1': a Hoof dropped NEXT turn. Next-turn mana = every board source's yield regardless of
    // tapped-ness (everything untaps; a dork cast this turn is no longer sick), minus the K eaten
    // sources (approximated at one mana each -- the pool eats Forests first), plus a land drop.
    if (nhoof != nullptr && nroute >= 0)
    {
        int full_sources = 0;
        bool land_in_hand = false;
        for (const Permanent& q : state.battlefield)
        {
            if (q.controller_index != controller) { continue; }
            const CardDefinition* qd = CardDatabase::Instance().LookupCached(q.card);
            if (!qd) { continue; }
            if (qd->tmpl != CardTemplate::BasicLand && qd->tmpl != CardTemplate::ManaDork
                && !qd->params.mana_rock) { continue; }
            full_sources += std::max(0, PermanentManaYield(state, q, *qd));
        }
        for (const Card& hc : ap.hand)
        {
            const CardDefinition* hd = CardDatabase::Instance().LookupCached(hc);
            if ((hd ? hd->card.IsLand() : hc.IsLand())) { land_in_hand = true; break; }
        }
        for (int k = 0; k <= kcap; ++k)
        {
            if (full_sources - k + (land_in_hand ? 1 : 0) < nroute) { continue; }
            int xc = n_cre, bp = base_power;
            if (nroute_no) { xc -= 1; bp = std::max(0, bp - 1); }
            const int cnt    = xc + k + 1;   // everyone attacks next turn, plus the Hoof
            const int attack = bp + per * k + nhoof->card.m_power.value_or(0) + cnt * cnt;
            if (attack >= opp_life) { return k; }
        }
    }

    // h=2: smallest K lethal over two swings.
    for (int k = 0; k <= kcap; ++k)
    { if (2 * (base_power + extra_p + per * k) >= opp_life) { return k; } }
    // No reachable window: the Elephants are the only clock ("go for broke").
    return kcap;
}

inline void FireCombatDamageCheatIntoPlay(GameState& state, int controller,
                                          const std::vector<int>& damaging_attacker_indices)
{
    if (damaging_attacker_indices.empty()) { return; }
    Player& ap = state.players[controller];
    for (int idx : damaging_attacker_indices)
    {
        if (idx < 0 || idx >= static_cast<int>(state.battlefield.size())) { continue; }
        const Permanent& src = state.battlefield[idx];
        if (src.controller_index != controller) { continue; }
        const CardDefinition* sd = CardDatabase::Instance().LookupCached(src.card);
        if (!sd || sd->params.combat_damage_puts_subtype_from_hand.empty()) { continue; }
        const std::vector<std::string>& want = sd->params.combat_damage_puts_subtype_from_hand;

        // A "permanent card" = anything that is not an instant/sorcery. Must carry a matching subtype.
        auto is_match = [&](const Card& c) -> bool {
            if (c.IsInstant() || c.IsSorcery()) { return false; }
            for (const std::string& sub : want) { if (CardHasSubtype(c, sub)) { return true; } }
            return false;
        };

        std::vector<int> cand_hand;   // hand indices of matching Goblin-permanent cards (chooser pool)
        for (int h = 0; h < static_cast<int>(ap.hand.size()); ++h)
        {
            // STAGED cards are impulse-EXILED (Rundvelt Hordemaster / Light Up the Stage): the
            // engine models them as in-hand-with-flag so they stay castable, but in the real game
            // they sit in EXILE and "put a ... card from your hand" (CR: Lackey names the hand
            // zone) cannot reach them. The REAL executor already cannot see them here (AIEngine
            // keeps staged cards outside the hand between main phases), but SIMULATED states carry
            // them merged -- so without this skip the search imagines a put reality then declines,
            // and commits follow-up actions against a phantom permanent (goblins gi123: a staged
            // Mogg War Marshal "put" then sacked -- the plan's victim never existed).
            if (ap.hand[h].m_is_staged) { continue; }
            const CardDefinition* hd = CardDatabase::Instance().LookupCached(ap.hand[h]);
            const Card& hc = hd ? hd->card : ap.hand[h];
            if (is_match(hc)) { cand_hand.push_back(h); }
        }
        // WHICH permanent to put is provider-owned (CombatCheatCandidates): the base rule is the
        // historical highest-MV/power/number pick, so this is byte-identical. Ranked best-first;
        // index 0 is the non-branching answer.
        const std::vector<int> ranked =
            ResolveProvider(state).CombatCheatCandidates(state, controller, *sd, cand_hand);
        // MTG_LACKEY_TRACE: DIAGNOSTIC (no play change). Sizes the decision -- how many matching
        // permanents the put actually chooses among, and what the ranked pick costs. One candidate
        // is forced; the arbitrary-pick risk only bites at two or more.
        static const bool s_lackey_trace = EnvOn("MTG_LACKEY_TRACE");
        if (s_lackey_trace && !cand_hand.empty())
        {
            const CardDefinition* pd = ranked.empty()
                ? nullptr : CardDatabase::Instance().LookupCached(ap.hand[ranked.front()]);
            std::fprintf(stderr, "[lackey] turn=%d cands=%d pick=%s mv=%d\n",
                         state.turn_number, static_cast<int>(cand_hand.size()),
                         pd ? pd->card.m_name.str().c_str() : "-",
                         pd ? pd->card.m_mana_cost.ManaValue() : -1);
        }
        if (ranked.empty()) { continue; }   // "may": nothing matching to put -> decline
        int best = ranked.front();
        // SEARCHED pick: the main-phase plan may have pinned an index into `ranked`
        // (Plan::lackey_choice -> GameState::scripted_cheat_choice). Clamped rather than dropped --
        // the enumerator sizes the axis off the hand as it stood before combat, and a cast in the
        // same plan can shrink it, leaving a pinned index past the end. Clamping makes that variant
        // a duplicate of the last (identical state -> the search tie-breaks to the first, which is
        // the provider's pick) instead of a silent decline.
        if (state.scripted_cheat_choice >= 0)
        {
            const std::size_t k = static_cast<std::size_t>(state.scripted_cheat_choice);
            best = ranked[std::min(k, ranked.size() - 1)];
        }
        state.scripted_cheat_choice = -1;   // consumed by this trigger
        // MTG_LACKEY_PREF: DIAGNOSTIC (no play change). Logs the REVEALED PREFERENCE of the search --
        // the candidate set that was actually available and the card the chosen plan actually put --
        // for REAL resolutions only (g_real_resolution; every rollout scope clears it). Run with a
        // wide MTG_LACKEY_WIDTH so the search sees every candidate, then aggregate pairwise: a pair
        // that splits ~100/0 is a STRICT dominance and the loser can be pruned safely; a pair that
        // splits ~60/40 is SITUATIONAL and must stay in the searched set.
        static const bool s_lackey_pref = EnvOn("MTG_LACKEY_PREF");
        if (s_lackey_pref && g_real_resolution && cand_hand.size() > 1)
        {
            std::string line = "[lackey-pref] chose=";
            line += ap.hand[best].m_name.str();
            line += " from=";
            for (std::size_t ci = 0; ci < cand_hand.size(); ++ci)
            {
                // '|', NOT ',': card names contain commas ("Muxus, Goblin Grandee", "Krenko, Mob
                // Boss"), and a comma-separated list silently splits them into phantom cards that
                // match nothing -- which made Muxus read as chosen 0/131 times on the first pass.
                if (ci) { line += "|"; }
                line += ap.hand[cand_hand[ci]].m_name.str();
            }
            // Which candidate names the controller ALREADY has on the battlefield. A second copy of
            // an ETB payoff (Muxus) is worth less than the first, so a preference that looks strict
            // overall may reverse once a copy is down -- that is a board-CONDITIONED effect a static
            // ranking cannot express, and the reason to check before pruning on the raw split.
            line += " inplay=";
            bool first_ip = true;
            for (int ci : cand_hand)
            {
                bool on_board = false;
                for (const Permanent& q : state.battlefield)
                { if (q.controller_index == controller && q.card.m_name == ap.hand[ci].m_name) { on_board = true; break; } }
                if (!on_board) { continue; }
                if (!first_ip) { line += "|"; }
                first_ip = false;
                line += ap.hand[ci].m_name.str();
            }
            // Board width, and whether the deck's top payoff is still findable. A lord's value scales
            // with the creatures it buffs, while a tutor's does not -- so a pair that looks like a
            // shutout in aggregate can reverse on a narrow board. Without this the data cannot tell
            // a STRICT preference from one that is merely strict AT THE USUAL BOARD WIDTH.
            int my_creatures = 0;
            for (const Permanent& q : state.battlefield)
            { if (q.controller_index == controller && q.card.IsCreature()) { ++my_creatures; } }
            int muxus_left = 0;
            for (const Card& q : ap.library) { if (q.m_name == "Muxus, Goblin Grandee") { ++muxus_left; } }
            // Opponent life, and whether the controller could just CAST each candidate right now.
            // Both are hypotheses for why a cheaper card ever beats a dearer one in this slot: an
            // ETB-damage creature can simply be lethal, and a card you can already afford wastes the
            // free put (the Lackey slot is for what you cannot pay for).
            std::string affordable;
            {
                ManaPool pool_now = AvailableManaPool(state, nullptr);
                for (int ci : cand_hand)
                {
                    const CardDefinition* cd = CardDatabase::Instance().LookupCached(ap.hand[ci]);
                    if (cd == nullptr || !pool_now.CanPay(cd->card.m_mana_cost)) { continue; }
                    if (!affordable.empty()) { affordable += "|"; }
                    affordable += ap.hand[ci].m_name.str();
                }
            }
            // Land count -> next turn's mana ceiling (lands + 1 land drop). The Lackey slot's real
            // opportunity cost is not "can I cast this NOW" but "will I get it anyway soon": a card
            // at or below next turn's mana arrives regardless, so spending the free put on it wins
            // only a turn of tempo, while a card above that ceiling is otherwise unreachable.
            int my_lands = 0;
            for (const Permanent& q : state.battlefield)
            { if (q.controller_index == controller && q.card.IsLand()) { ++my_lands; } }
            std::fprintf(stderr, "%s creatures=%d muxus_in_lib=%d opplife=%d lands=%d castable=%s\n",
                         line.c_str(), my_creatures, muxus_left,
                         state.players[1 - controller].life, my_lands, affordable.c_str());
        }

        // Human play (--claude-play/viewer): let the player pick WHICH Goblin permanent to put, or
        // decline (it is a "may"). Nulled by RevealLogPause for every search/rollout scope, so this
        // fires only on the REAL combat-damage resolution and autonomous play is byte-identical (chooser
        // null -> the heuristic `best` stands, no allocation). Default = the highest-MV heuristic pick.
        if (g_play_lackey_chooser)
        {
            std::vector<Card> candidates;
            int heur_ci = -1;
            for (int ci = 0; ci < static_cast<int>(cand_hand.size()); ++ci)
            {
                candidates.push_back(ap.hand[cand_hand[ci]]);
                if (cand_hand[ci] == best) { heur_ci = ci; }
            }
            int picked = (*g_play_lackey_chooser)(state, controller, src.card.m_name.str(),
                                                  candidates, heur_ci);
            if (picked == -1) { continue; }                                   // human declined the put
            if (picked >= 0 && picked < static_cast<int>(cand_hand.size()))
            { best = cand_hand[picked]; }                                     // else out-of-range -> heuristic
        }

        Card raw = ap.hand[best];
        ap.hand.erase(ap.hand.begin() + best);
        const CardDefinition* d = CardDatabase::Instance().LookupCached(raw);
        Permanent perm;
        perm.card              = d ? d->card : raw;
        perm.card.m_number     = raw.m_number;
        perm.controller_index  = controller;
        perm.owner_index       = controller;
        perm.entered_this_turn = true;
        // `src` is a REFERENCE INTO state.battlefield, and the push_back below can REALLOCATE that
        // vector -- after which any later read of `src` is a use-after-free. InternedName holds a
        // const std::string* into the global registry, so the stale read yields a WILD pointer that
        // is then dereferenced: usually the freed block still holds the old pointer and the game
        // plays correctly (which is why ground truth is stable), but under a different allocator
        // state it is garbage or a crash. Caught by ASAN 2026-08-25 at the EmitReveal below, on
        // Goblin Lackey (the only card with combat_damage_puts_subtype_from_hand). Copy the name out
        // BEFORE the vector is touched -- InternedName is trivially copyable (a pointer), so this is
        // free and byte-identical. Do NOT use `src` past this point.
        const InternedName src_name = src.card.m_name;
        state.battlefield.push_back(perm);
        const int slot = static_cast<int>(state.battlefield.size()) - 1;
        // Report WHAT was cheated in. A Lackey put moves a card from a HIDDEN zone (hand) to a public
        // one, exactly like a tutor fetch, so it rides the same reveal channel -- otherwise the only
        // record is the viewer's own click label, which means an AI-resolved game, a rendered log and
        // (for 1v1) the OPPONENT all see nothing. Emitted BEFORE the ETB cascade so the put reads
        // before anything it triggers. Gated on RevealVisible -> byte-identical when nobody listens.
        //
        // This was viewer_only at first (a new log entry folds into the play digest and forces a
        // rebaseline), which left post-hoc A/B forensics on this deck blind to the cheat-into-play
        // choices -- the drops had to be reverse-engineered from per-phase board deltas. Dropped at
        // the Goblins tutor-ranking rebaseline, which pays that digest cost anyway. The site only
        // fires for combat_damage_puts_subtype_from_hand, so ONLY Goblins digests move.
        if (RevealVisible())
        {
            EmitReveal(state.turn_number, src_name.str() + " (put)",
                       { raw.m_number }, { raw.m_name.str() }, { raw.m_number }, {},
                       /*dispositions*/ { "\xE2\x86\x92 battlefield (from hand)" });
        }
        FireEtbWatchers(state, controller, slot);   // in case a put permanent is ever a Dragon
        FireOwnEtbTriggers(state, controller, slot);   // fire the put permanent's own ETB triggers
        // Legend rule (CR 704.5j) -- a STATE-BASED action, checked continuously, so it must run the
        // moment the duplicate enters. It was missing here: the other put path (a Vial deploy) does
        // enforce it, but this put resolves in the COMBAT-DAMAGE step, i.e. AFTER the combat-start
        // enforcement in GameEngine::CombatPhase / SimulateCombat. A second Muxus or Krenko cheated
        // in by Lackey therefore survived until the NEXT turn's combat start, giving a free extra
        // body and double-counting any static buff for a full turn cycle.
        if (state.battlefield[slot].card.HasSupertype(Supertype::Legendary))
        { EnforceLegendRule(state, controller); }
    }
}

// Consume `cost` from a flat ManaPool (assumes pool.CanPay(cost) is true). Colored pips are paid
// from their own colour first (wild covers a shortfall); generic is paid from off-colours / wild
// first, red last, so scarce coloured mana is preserved for coloured firebreathing pips.
inline void PayFromPool(ManaPool& pool, const ManaCost& cost_in)
{
    // Hybrid pips: resolve to the first concrete assignment the pool can actually pay (bits==0 =
    // first-listed-colour preference), then pay flat. CanPay(cost_in) was the caller's guarantee,
    // so an assignment exists.
    ManaCost cost = cost_in;
    if (cost_in.hybrid_count > 0)
    {
        cost = cost_in.ExpandHybrids(0);
        for (unsigned bits = 0; bits < (1u << cost_in.hybrid_count); ++bits)
        {
            ManaCost c = cost_in.ExpandHybrids(bits);
            if (pool.CanPayFlat(c)) { cost = c; break; }
        }
    }
    auto pay_colored = [&](int need, int& src)
    {
        int use = std::min(need, src); src -= use; need -= use;
        if (need > 0) { pool.wild -= need; }   // CanPay guaranteed wild covers the remainder
    };
    pay_colored(cost.white,     pool.white);
    pay_colored(cost.blue,      pool.blue);
    pay_colored(cost.black,     pool.black);
    pay_colored(cost.red,       pool.red);
    pay_colored(cost.green,     pool.green);
    pay_colored(cost.colorless, pool.colorless);
    int gen = cost.generic;
    int* order[] = { &pool.colorless, &pool.wild, &pool.white, &pool.blue,
                     &pool.black, &pool.green, &pool.red };
    for (int* src : order)
    {
        if (gen <= 0) { break; }
        int use = std::min(gen, *src); *src -= use; gen -= use;
    }
}

// Firebreathing: spend LEFTOVER combat mana on activated +power pumps of the ATTACKERS, turning
// mana into face damage. `pool` is the leftover mana built by the caller (BuildPool in the
// rollout / AIEngine::BuildAvailableMana in the executor -- byte-identical for the same state, so
// both worlds pump identically = lockstep). In a goldfish every point of attacker power is +1 to
// the face and combat is the last mana use for these decks, so spending all affordable mana on the
// most damage-efficient activation is the search-optimal, deterministic resolution (not a quality
// heuristic; the params exist only on Scourge/Lathliss so every other deck is byte-identical). The
// real mana sources are NOT tapped (no post-combat mana sink for firebreathing decks). Pump lands
// as temp_power_bonus so the combat damage loop and the win projection both see the extra power.
// Cheap pre-scan so non-firebreathing decks skip the leftover-pool build entirely (zero overhead
// -> byte-identical AND no perf cost). True iff `controller` controls a firebreathing source.
inline bool ControlsFirebreathingSource(const GameState& state, int controller)
{
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && (d->params.firebreathing_cost.has_value() || d->params.team_pump_cost.has_value()))
        { return true; }
    }
    return false;
}

// Spend LEFTOVER combat mana greedily (best damage/mana ratio) on attacker pumps. Returns the number
// of pump ACTIVATIONS performed. `max_activations` caps how many (default INT_MAX = the greedy full
// spend, byte-identical to before). Human play passes a smaller k so the player can hold mana back
// (viewer #4, via the turn-keyed --firebreathe side-channel); autonomous always uses the default.
inline int ApplyFirebreathing(GameState& state, int controller,
                              const std::vector<int>& attacker_indices, ManaPool pool,
                              int max_activations = std::numeric_limits<int>::max())
{
    if (attacker_indices.empty()) { return 0; }
    int activations = 0;
    for (;;)
    {
        if (activations >= max_activations) { break; }   // human-chosen budget reached (#4)
        int    best_kind = 0;      // 1 = self (pump one attacker), 2 = team (pump matching attackers)
        int    best_self_idx = -1;
        int    best_src_idx  = -1;
        double best_ratio = 0.0;   // damage per mana-value

        // Self firebreathing (Scourge {R}: this creature gets +1/+0).
        for (int idx : attacker_indices)
        {
            const Permanent& p = state.battlefield[idx];
            if (p.controller_index != controller) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (!d || !d->params.firebreathing_cost.has_value() || d->params.firebreathing_power <= 0)
            { continue; }
            // A firebreather with a DISCARD rider (Burning-Fist Minotaur) is deliberately NOT
            // converted here. This loop's damage-per-MANA ratio cannot price a CARD, and in the
            // deck that runs it an emptied hand is itself a payoff (Neheb's hand-size anthem), so
            // spending the hand for +2/+0 is a judgment the SEARCH must own -- it is enumerated as
            // a main-phase Action::Kind::ActivatePump instead. Leaving it in would be exactly the
            // greedy-step-inside-the-searched-window this repo forbids.
            if (d->params.firebreathing_discard) { continue; }
            const ManaCost& c = d->params.firebreathing_cost.value();
            if (!pool.CanPay(c)) { continue; }
            double ratio = static_cast<double>(d->params.firebreathing_power)
                         / std::max(1, c.ManaValue());
            if (ratio > best_ratio + 1e-9)
            { best_ratio = ratio; best_kind = 1; best_self_idx = idx; }
        }

        // Team firebreathing (Lathliss {1}{R}: Dragons you control get +1/+0). Damage = power x
        // (attacking creatures matching team_pump_subtypes). The Lathliss itself need not attack.
        const int bf_size = static_cast<int>(state.battlefield.size());
        for (int si = 0; si < bf_size; ++si)
        {
            const Permanent& src = state.battlefield[si];
            if (src.controller_index != controller) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(src.card);
            if (!d || !d->params.team_pump_cost.has_value() || d->params.team_pump_power <= 0)
            { continue; }
            const ManaCost& c = d->params.team_pump_cost.value();
            if (!pool.CanPay(c)) { continue; }
            int matching = 0;
            for (int idx : attacker_indices)
            {
                const Permanent& a = state.battlefield[idx];
                if (a.controller_index != controller) { continue; }
                bool m = d->params.team_pump_subtypes.empty();
                for (const std::string& sub : d->params.team_pump_subtypes)
                { if (a.is_animated || CardHasSubtype(a.card, sub)) { m = true; break; } }
                if (m) { ++matching; }
            }
            if (matching <= 0) { continue; }
            double ratio = static_cast<double>(d->params.team_pump_power * matching)
                         / std::max(1, c.ManaValue());
            if (ratio > best_ratio + 1e-9)
            { best_ratio = ratio; best_kind = 2; best_src_idx = si; }
        }

        if (best_kind == 0) { break; }
        if (best_kind == 1)
        {
            const CardDefinition* d =
                CardDatabase::Instance().LookupCached(state.battlefield[best_self_idx].card);
            PayFromPool(pool, d->params.firebreathing_cost.value());
            state.battlefield[best_self_idx].temp_power_bonus += d->params.firebreathing_power;
            // Inferno of the Star Mounts: "When its power becomes 20 THIS WAY, it deals 20 damage
            // to any target." The crossing increment came from this ability (we are inside its
            // activation), so the equality test here is exactly the trigger condition; other pumps
            // that contributed to the total legitimately count. "Any target" collapses to the
            // opponent's face (the Scourge precedent) and is dealt as LIFE LOSS, so the rollout's
            // win projection counts it. Inert (threshold 0) for every other firebreather.
            if (d->params.firebreathing_threshold_power > 0
                && d->params.firebreathing_threshold_damage > 0
                && state.battlefield[best_self_idx].EffectivePower()
                       == d->params.firebreathing_threshold_power)
            {
                const int opp = 1 - controller;
                const int before = state.players[opp].life;
                state.players[opp].life -= d->params.firebreathing_threshold_damage;
                state.opponent_lost_life_this_turn = true;
                if (g_play_event_sink && !g_tap_speculating)
                {
                    EmitPlayEvent(state.turn_number, "damage",
                                  state.battlefield[best_self_idx].card.m_name.str()
                                  + " (power " + std::to_string(d->params.firebreathing_threshold_power)
                                  + "): " + std::to_string(d->params.firebreathing_threshold_damage)
                                  + " to opponent (" + std::to_string(before) + "\xE2\x86\x92"
                                  + std::to_string(before - d->params.firebreathing_threshold_damage) + ")");
                }
            }
        }
        else
        {
            const CardDefinition* d =
                CardDatabase::Instance().LookupCached(state.battlefield[best_src_idx].card);
            PayFromPool(pool, d->params.team_pump_cost.value());
            for (int idx : attacker_indices)
            {
                Permanent& a = state.battlefield[idx];
                if (a.controller_index != controller) { continue; }
                bool m = d->params.team_pump_subtypes.empty();
                for (const std::string& sub : d->params.team_pump_subtypes)
                { if (a.is_animated || CardHasSubtype(a.card, sub)) { m = true; break; } }
                if (m) { a.temp_power_bonus += d->params.team_pump_power; }
            }
        }
        ++activations;
    }
    return activations;
}

// ---- Suspend: cast off suspend (the SHARED free-cast site) ----------------------------------
// When a suspended card's last time counter is removed at upkeep (CR 702.62e), it is CAST off
// suspend WITHOUT paying its mana cost -- a real SPELL CAST. This one helper is that cast site, called
// IDENTICALLY from the executor (GameEngine::UpkeepStep) and the rollout (SimulateEndAndStartNextTurn)
// so the two worlds enter the permanent in lockstep. Routing the arrival through here (not a side path
// that just push_backs a Permanent) is deliberate: it is the SINGLE place Dragonstorm's future
// spells_cast_this_turn increment will live, so an off-suspend arrival automatically counts +1 toward
// storm. Suspending (paying {0}) and sacrificing are NOT casts and do NOT come through here. The card
// enters as its permanent; a Dragon fires FireEtbWatchers (Lotus Bloom is a colourless artifact -> no-op).
inline void CastOffSuspend(GameState& state, int controller, const Card& card)
{
    const CardDefinition* def = CardDatabase::Instance().LookupCached(card);
    if (!def) { return; }
    // STORM counter (Dragonstorm): casting off suspend IS a spell cast (CR 702.62e) -> count it here,
    // the single shared arrival path (executor GameEngine::UpkeepStep + rollout
    // SimulateEndAndStartNextTurn, both after the turn-start reset). A Lotus Bloom arriving on the same
    // turn as a Dragonstorm therefore adds +1 storm. Suspending ({0}) and sacrificing are NOT casts and
    // do not come through here. Byte-identical for every deck that never suspends (this path never runs).
    ++state.spells_cast_this_turn;
    Permanent perm;
    perm.card              = def->card;
    perm.card.m_number     = card.m_number;   // preserve the per-copy id for logging/state key
    perm.controller_index  = controller;
    perm.owner_index       = controller;
    perm.entered_this_turn = true;
    state.battlefield.push_back(perm);
    if (g_play_event_sink)   // nulled by RevealLogPause during search/rollout -> byte-identical
    {
        EmitPlayEvent(state.turn_number, "suspend",
                      "\xE2\x8C\x9B " + card.m_name.str() + " -- cast off suspend (enters play)");
    }
    FireEtbWatchers(state, controller, static_cast<int>(state.battlefield.size()) - 1);
}

// Apply a SUSPEND action ({0}): move the first matching in-hand card to Player::suspended_cards with
// arrive_turn = turn + its suspend_time_counters. Shared by the rollout (ApplyPlanDirect) and the
// executor (AIEngine::TakeTurn) so the two worlds exile the card identically (lockstep). No-op if the
// card isn't in hand / isn't suspendable.
inline void ApplySuspend(GameState& state, int controller, const std::string& name)
{
    Player& pl = state.players[controller];
    for (auto it = pl.hand.begin(); it != pl.hand.end(); ++it)
    {
        if (it->m_name != name) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(*it);
        if (!d || d->params.suspend_time_counters <= 0) { continue; }
        SuspendedCard sc;
        sc.card        = *it;
        sc.arrive_turn = state.turn_number + d->params.suspend_time_counters;
        pl.suspended_cards.push_back(std::move(sc));
        if (g_play_event_sink)   // nulled during search/rollout -> byte-identical
        {
            EmitPlayEvent(state.turn_number, "suspend",
                          "\xE2\x8C\x9B " + name + " -- suspended (arrives T"
                          + std::to_string(sc.arrive_turn) + ")");
        }
        pl.hand.erase(it);
        return;
    }
}

// Apply a SacForMana ability: find the untapped source in play by its per-instance id, float `amount`
// mana of `color` into the turn-scoped reserve (AddChosenColorFloat), then SACRIFICE the source (to the
// graveyard). Shared by the rollout and the executor (lockstep). No-op if the source is gone/tapped.
// `victim_id` == 0 (default): Lotus Bloom -- tap + sacrifice the SOURCE for the float.
// `victim_id` != 0: Skirk Prospector -- the source STAYS; sacrifice the chosen victim Goblin (which
// fires its death-watchers) and float the colour. Reuses the entire SacForMana solver coupling.
inline void ApplySacForMana(GameState& state, int controller, int sac_source_id,
                            const std::string& color, int amount, int victim_id = 0)
{
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        Permanent& p = state.battlefield[i];
        if (p.controller_index != controller) { continue; }
        if (p.card.m_number != sac_source_id) { continue; }
        // TOKENS are unnumbered (m_number 0), so with sac_source_id == 0 this scan can land on the
        // WRONG token first (a 1/1 Goblin ahead of the Treasure in battlefield order). The old
        // `return`s below then silently NO-OPED the whole sacrifice -- state unchanged, the plan
        // re-offered, and the play-viewer segment loop span forever (Mirrorwing play_invariants
        // runaway, 2026-08-11; latent for any deck mixing creature tokens with Treasures). Same-id
        // copies of a sac source are fungible, so on a mismatch KEEP SCANNING (continue) for an
        // id-matching permanent that IS a valid, untapped source; only a full-scan miss no-ops.
        // Byte-identical whenever the first id-match was already the valid source (every numbered
        // Lotus, and every token board with the source first).
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        const bool lotus = d->params.sac_for_mana_amount > 0;
        const bool skirk = d->params.sac_creature_outlet && !d->params.sac_outlet_add_mana_color.empty();
        if (!lotus && !skirk) { continue; }
        if (lotus && p.tapped)  { continue; }   // Lotus taps as part of the cost; a tapped one can't
        // Skirk MULTI-SAC BURST: the repeatable "Sacrifice a Goblin: Add {R}" can ramp in one turn.
        // ritual_float carries the TOTAL floated (k * per-sac), so a burst is `amount > per`. Sac k
        // canonical Goblins (token/weakest first, the source LAST -- so a full sac-all sacs Skirk itself
        // only on the final activation, which is rules-legal), floating `per` and firing OnCreatureDies
        // each (Pashalik ping / Rundvelt impulse / Mogg death token per Goblin). count==1 falls through
        // to the single-sac path below (BYTE-IDENTICAL). Shared by executor + rollout (every caller passes
        // a.ritual_float), so lockstep is automatic.
        // Captured BEFORE any sacrifice below: erasing from the battlefield invalidates `p`.
        const std::string src_name = p.card.m_name.str();
        const std::string need_sub = d->params.sac_creature_requires_subtype;
        if (skirk)
        {
            const int per = std::max(1, d->params.sac_outlet_add_mana_amount);
            const int count = amount / per;
            if (count > 1)
            {
                // DIG INSTRUMENT (MTG_SAC_TRACE, default off): per-sac victim picks for the burst,
                // printed by BOTH worlds (shared helper) -- diff apply vs exec to find the first
                // diverging victim (gi=149 lockstep dig 2026-08-15).
                static const bool s_sac_trace = std::getenv("MTG_SAC_TRACE") != nullptr
                                            && std::string(std::getenv("MTG_SAC_TRACE")) != "0";
                for (int n = 0; n < count; ++n)
                {
                    const int vid = CanonicalSacVictim(state, controller, sac_source_id, need_sub);
                    if (s_sac_trace)
                    {
                        std::string vname = "(none)";
                        for (const Permanent& q : state.battlefield)
                        { if (q.controller_index == controller && q.card.m_number == vid) { vname = q.card.m_name.str(); break; } }
                        std::fprintf(stderr, "[sac] T%d burst %d/%d src=%s vid=%d (%s) bf=%d\n",
                                     state.turn_number, n + 1, count, src_name.c_str(), vid,
                                     vname.c_str(), static_cast<int>(state.battlefield.size()));
                    }
                    if (vid < 0) { break; }   // ran out of Goblins to sacrifice
                    // Human play picks each victim in turn (one prompt per sac in the burst); with no
                    // chooser this is -1 and the card-number path below runs byte-identically.
                    const int bidx = ChooseSacOutletVictimIndex(state, controller, sac_source_id,
                                                                need_sub, vid, src_name);
                    AddChosenColorFloat(state, color, per);
                    if (bidx >= 0) { SacrificePermanentAt(state, controller, bidx); continue; }
                    for (int j = 0; j < static_cast<int>(state.battlefield.size()); ++j)
                    {
                        Permanent& q = state.battlefield[j];
                        if (q.controller_index != controller || q.card.m_number != vid) { continue; }
                        SacrificePermanentAt(state, controller, j);   // per-sac death triggers (lockstep)
                        break;
                    }
                }
                return;
            }
        }
        AddChosenColorFloat(state, color, amount);   // float the chosen colour into state.floating_mana
        if (g_play_event_sink)   // nulled during search/rollout -> byte-identical
        {
            EmitPlayEvent(state.turn_number, "mana",
                          "\xF0\x9F\xAA\xB7 " + p.card.m_name.str() + " -- sacrifice: add "
                          + std::to_string(amount) + " " + (color.empty() ? "wild" : color));
        }
        if (victim_id != 0 && skirk)
        {
            // Skirk: the source stays; sacrifice the chosen victim Goblin. The victim was picked at
            // ENUMERATION time (CanonicalSacVictim, baked into Action::sac_victim_id), so human play
            // re-asks here against the real resolution board -- otherwise the human's only say in a
            // single-sac activation would be that they made it at all.
            const int bidx = ChooseSacOutletVictimIndex(state, controller, sac_source_id,
                                                        need_sub, victim_id, src_name);
            if (bidx >= 0) { SacrificePermanentAt(state, controller, bidx); return; }
            // Stale-victim fallback (see ApplySacCreatureOutlet): an earlier action may have
            // consumed the baked victim; TOKEN victims (id >= 1000, legacy 0) are fungible, so
            // re-pick against the live board rather than ghosting the float. A missing REAL-card
            // victim is a failed plan premise (the search imagined the victim into existence --
            // goblins gi123) and must NO-OP, never substitute a live creature. Lockstep (shared
            // helper, both worlds).
            const bool victim_was_token = (victim_id == 0 || victim_id >= 1000);
            for (int attempt = 0; attempt < 2; ++attempt)
            {
                for (int j = 0; j < static_cast<int>(state.battlefield.size()); ++j)
                {
                    Permanent& q = state.battlefield[j];
                    if (q.controller_index != controller || q.card.m_number != victim_id) { continue; }
                    SacrificePermanentAt(state, controller, j);   // Pashalik ping / death token etc.
                    return;
                }
                if (attempt > 0 || !victim_was_token) { break; }
                victim_id = CanonicalSacVictim(state, controller, sac_source_id, need_sub);
                if (victim_id < 0) { break; }
            }
            return;
        }
        state.players[controller].graveyard.push_back(p.card);   // Lotus: sacrifice the source
        state.battlefield.erase(state.battlefield.begin() + i);
        return;
    }
}

// Process the controller's SUSPEND arrivals at the start of their turn (upkeep): remove one time
// counter from each suspended card (modelled by arrive_turn) and cast any whose last counter is now
// gone. Called from BOTH GameEngine::UpkeepStep (executor) and SimulateEndAndStartNextTurn (rollout)
// AFTER untap, so the arrived permanent is untapped and available this turn. Empty suspended list ->
// no-op -> byte-identical for every deck without a suspend card.
inline void ProcessSuspendArrivals(GameState& state, int controller)
{
    Player& pl = state.players[controller];
    if (pl.suspended_cards.empty()) { return; }
    std::vector<SuspendedCard> remaining;
    remaining.reserve(pl.suspended_cards.size());
    // Snapshot then clear so a re-entrant FireEtbWatchers (a suspended Dragon would ping/spawn) cannot
    // observe a half-processed list; arrivals are cast in the order they were suspended.
    std::vector<SuspendedCard> snap = std::move(pl.suspended_cards);
    pl.suspended_cards.clear();
    for (SuspendedCard& sc : snap)
    {
        if (sc.arrive_turn <= state.turn_number) { CastOffSuspend(state, controller, sc.card); }
        else                                     { remaining.push_back(std::move(sc)); }
    }
    // Re-attach anything an arrival's cascade may have added, plus the not-yet-arrived remainder.
    for (SuspendedCard& sc : pl.suspended_cards) { remaining.push_back(std::move(sc)); }
    pl.suspended_cards = std::move(remaining);
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
// RAD COUNTERS (Mariposa Military Base). The rule is not printed on the card -- it is inherent to
// the counter type: "At the beginning of each player's precombat main phase, that player mills a
// number of cards equal to the number of rad counters they have. For each NONLAND card milled this
// way, that player loses 1 life and removes one rad counter from themselves."
//
// Modelling it is what makes taking the counters a real DECISION rather than free upside. Without
// the mill, `etb_optional_tapped_rad` would be strictly positive (a discount with no downside) and
// the search would accept it every time -- the over-acceptance class that surfaces later as
// [fd-diverge]. See LandPlayOptions::rad_mode.
//
// The TIMING is the interesting part and it is why the trade is close: the land enters tapped, so it
// cannot be tapped for the discounted draw on the turn it arrives; the next thing that happens is
// this trigger, at the beginning of the following precombat main -- BEFORE the main-phase window
// where that draw could be activated. So the counters face the mill before the discount is ever
// spendable, and every nonland milled takes one away. Whether the discount survives long enough to
// be used is therefore a property of the deck's land ratio, which is a thing to MEASURE, not assume.
//
// Called at the head of the precombat main in BOTH worlds (GameEngine::MainPhase and the rollout's
// per-turn main) so the executor realises what the search scored. No-op at 0 counters, which is
// every player of every other deck.
inline void ApplyRadMill(GameState& state, int controller)
{
    Player& p = state.players[controller];
    if (p.rad_counters <= 0) { return; }
    const int n = std::min<int>(p.rad_counters, static_cast<int>(p.library.size()));
    for (int i = 0; i < n; ++i)
    {
        const Card c = p.library.front();
        p.library.erase(p.library.begin());
        p.graveyard.push_back(c);
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        // A milled NONLAND costs 1 life and removes a counter. The land/nonland test reads the
        // DEFINITION, not the zone-card mask: a card outside the battlefield carries empty type
        // masks, so c.IsLand() would be false for every card here and the rad counters would never
        // decay (see the zone-card placeholder-mask trap).
        if (d && !d->card.IsLand())
        {
            p.life -= 1;
            if (p.rad_counters > 0) { --p.rad_counters; }
        }
    }
}

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

// ---- Creature Giving upkeep triggers (Varchild's War-Riders / Defense of the Heart) -----------
// Both are UPKEEP effects of the active player, applied IDENTICALLY at the executor's upkeep
// (GameEngine::RestOfUpkeep) and the rollout's simulated turn-start (TurnSolver::
// SimulateEndAndStartNextTurn), in this order: cumulative-upkeep gifts FIRST, then the Defense of
// the Heart check -- the controller-optimal trigger ordering (the fresh Survivors count toward
// DotH's "opponent controls three or more creatures"). Param-gated -> byte-identical elsewhere.

// Varchild's War-Riders cumulative upkeep: +1 age counter, then the OPPONENT creates age_counters
// tokens (upkeep_token_* spec; 1/1 red Survivor). ALWAYS PAID -- weakly dominant vs the passive
// opponent (the gifts only feed our drains / DotH, and the 3/4 body is kept); pay-vs-sacrifice is
// a disclosed auto-decision. Each gift enters via CreateToken -> the enter-watcher cascade
// (Suture Priest drains). Iterates over the pre-existing battlefield size only (the created
// tokens are appended and must not re-trigger anything here).
inline void PerformUpkeepCumulativeGifts(GameState& state)
{
    const int active = state.active_player_index;
    const int n = static_cast<int>(state.battlefield.size());
    for (int i = 0; i < n; ++i)
    {
        Permanent& p = state.battlefield[i];
        if (p.controller_index != active) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d || !d->params.cumulative_upkeep_opp_token) { continue; }
        // CreateToken APPENDS to state.battlefield, which reallocates it and leaves `p` dangling --
        // so the loop bound must not be re-read off `p` after the first token (that read is a
        // use-after-free: ASAN 2026-08-25, READ of size 4 = age_counters). Latch the count first.
        // The token subtypes come from `d`, which points into the CardDatabase and is unaffected.
        const int gifts = ++p.age_counters;   // do NOT touch `p` past this point
        for (int k = 0; k < gifts; ++k)
        {
            CreateToken(state, 1 - active, d->params.upkeep_token_power,
                        d->params.upkeep_token_toughness, d->params.upkeep_token_subtypes);
        }
    }
}

// Defense of the Heart: "At the beginning of your upkeep, if an opponent controls three or more
// creatures, sacrifice this enchantment, search your library for up to two creature cards, put
// those cards onto the battlefield, then shuffle." The intervening-if is checked here (trigger
// time == resolution time in this engine's upkeep). WHICH creatures + their enter ORDER is the
// provider's SacTutorPutList (default: closed-form immediate-drain maximisation); a human
// override rides g_play_sac_tutor_chooser (nulled by RevealLogPause -> search/rollout
// byte-identical). Each put creature enters through the shared cascades, so a put Hunted
// Phantasm gifts its Goblins (draining per Suture Priest) and a put Massacre Wurm sweeps.
inline void PerformUpkeepSacTutor(GameState& state)
{
    const int active = state.active_player_index;
    // Snapshot the triggering copies first: resolving one (sacrifice + puts) mutates the
    // battlefield. Two copies both fire if the condition holds for each in turn.
    for (;;)
    {
        int   src_index = -1;
        const CardDefinition* src_def = nullptr;
        for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
        {
            const Permanent& p = state.battlefield[i];
            if (p.controller_index != active) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (!d || d->params.upkeep_sac_tutor_creatures <= 0) { continue; }
            if (CreatureCount(state, 1 - active) < d->params.upkeep_sac_tutor_opp_min) { continue; }
            src_index = i; src_def = d; break;
        }
        if (src_index < 0) { return; }

        const CardParams& pp       = src_def->params;
        const int         max_puts = pp.upkeep_sac_tutor_creatures;
        const std::string src_name = state.battlefield[src_index].card.m_name.str();
        Player&           ap       = state.players[active];

        // Sacrifice the enchantment (the cost of the resolution) BEFORE the search.
        ap.graveyard.push_back(state.battlefield[src_index].card);
        state.battlefield.erase(state.battlefield.begin() + src_index);

        // The put-list: provider heuristic, overridable by the human chooser.
        std::vector<std::string> put_list =
            ResolveProvider(state).SacTutorPutList(state, active, pp, max_puts);
        if (g_play_sac_tutor_chooser)
        {
            // Candidates = every library creature card (one entry per copy), in library order.
            std::vector<Card> candidates;
            std::vector<int>  lib_slots;
            for (int i = 0; i < static_cast<int>(ap.library.size()); ++i)
            {
                const CardDefinition* d = CardDatabase::Instance().LookupCached(ap.library[i]);
                if (d && d->card.IsCreature()) { candidates.push_back(ap.library[i]); lib_slots.push_back(i); }
            }
            if (!candidates.empty())
            {
                // The heuristic's subset, mapped onto candidate indices (first matching copies).
                std::vector<int> heur;
                {
                    std::vector<bool> used(candidates.size(), false);
                    for (const std::string& nm : put_list)
                    {
                        for (int c = 0; c < static_cast<int>(candidates.size()); ++c)
                        {
                            if (used[c] || candidates[c].m_name.str() != nm) { continue; }
                            used[c] = true; heur.push_back(c); break;
                        }
                    }
                }
                std::vector<int> chosen = (*g_play_sac_tutor_chooser)(
                    state, active, src_name, candidates, max_puts, heur);
                put_list.clear();
                for (int c : chosen)
                {
                    if (c >= 0 && c < static_cast<int>(candidates.size())
                        && static_cast<int>(put_list.size()) < max_puts)
                    { put_list.push_back(candidates[c].m_name.str()); }
                }
            }
        }

        // The chosen creatures enter SIMULTANEOUSLY ("put those cards onto the battlefield"),
        // then their enter-triggers resolve, in list order. Pass 1 moves every card onto the
        // battlefield; pass 2 fires the shared cascades. This is what makes a double Massacre
        // Wurm put drain 4 per swept creature (each death is seen by BOTH Wurms) and lets two
        // simultaneously-put enter-watchers see each other (the paired-Soul-Warden ruling).
        // Pass 2 re-finds each put by its per-copy card number: an earlier cascade's sweep can
        // erase lower battlefield slots (tokens carry no number, so puts are unambiguous).
        int puts = 0;
        std::vector<std::pair<std::string, int>> placed;   // (name, per-copy number)
        for (const std::string& nm : put_list)
        {
            if (puts >= max_puts) { break; }
            int idx = -1;
            for (int i = 0; i < static_cast<int>(ap.library.size()); ++i)
            { if (ap.library[i].m_name == nm) { idx = i; break; } }
            if (idx < 0) { continue; }
            Card raw = ap.library[idx];
            const CardDefinition* d = CardDatabase::Instance().LookupCached(raw);
            if (!d || !d->card.IsCreature()) { continue; }
            ap.library.erase(ap.library.begin() + idx);
            Permanent perm;
            perm.card              = d->card;
            perm.card.m_number     = raw.m_number;
            perm.controller_index  = active;
            perm.owner_index       = active;
            perm.entered_this_turn = true;
            state.battlefield.push_back(perm);
            placed.emplace_back(nm, raw.m_number);
            ++puts;
        }
        for (const auto& pl : placed)
        {
            int slot = -1;
            for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
            {
                const Permanent& q = state.battlefield[i];
                if (q.controller_index == active && !q.is_token
                    && q.card.m_number == pl.second && q.card.m_name == pl.first)
                { slot = i; break; }
            }
            if (slot < 0) { continue; }
            FireEtbWatchers(state, active, slot);   // universal cascade (enter-watchers fire here)
            FireOwnEtbTriggers(state, active, slot);   // param-gated ETBs: Phantasm gift / Wurm sweep
        }

        // "...then shuffle" (deterministic + lockstep). ON BY DEFAULT; MTG_NO_SEARCH_SHUFFLE opts out.
        ShuffleAfterSearch(state, active);

        if (g_play_event_sink)   // nulled by RevealLogPause during search/rollout -> byte-identical
        {
            EmitPlayEvent(state.turn_number, "tutor",
                          "\xF0\x9F\x92\x9A " + src_name + ": " + std::to_string(puts)
                          + " creature(s) onto the battlefield");
        }
    }
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
    // WHICH copy survives is the controller's choice (CR 704.5j) and is provider-owned
    // (LegendKeepIndex); the base rule keeps the OLDEST (lowest battlefield index), which is what
    // the previous erase-as-you-scan loop did, so this is byte-identical.
    //
    // Grouped first, then resolved, because a provider may keep a copy that is not the first -- the
    // old loop could only ever keep the first. Graveyard order stays ASCENDING by battlefield index
    // (as before); the erase runs descending so the indices stay valid.
    std::vector<std::pair<std::string, std::vector<int>>> groups;   // name -> indices, ascending
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != controller_index
            || !p.card.HasSupertype(Supertype::Legendary)) { continue; }
        bool found = false;
        for (auto& g : groups) { if (g.first == p.card.m_name) { g.second.push_back(i); found = true; break; } }
        if (!found) { groups.emplace_back(p.card.m_name.str(), std::vector<int>{ i }); }
    }
    std::vector<int> doomed;
    for (const auto& g : groups)
    {
        if (g.second.size() < 2) { continue; }
        const int keep = ResolveProvider(state).LegendKeepIndex(state, controller_index, g.second);
        // MTG_TRACE=legend: is "keep the original" ever actually WRONG? One copy dies either way and
        // the ETB is already banked before this state-based action runs, so the choice is purely
        // about WHICH BODY survives -- i.e. it can only matter when the copies differ in a way that
        // affects what the survivor can do. `differs` counts exactly that; a group where every copy
        // is in the same state is a decision with no content, however often it fires. Measure the
        // contested rate BEFORE building a rule, the way the retrace/pitch pair showed matters.
        if (TRACE_ON("legend") && g_real_resolution)
        {
            const Permanent& a = state.battlefield[g.second.front()];
            bool d_tap = false, d_sick = false, d_dmg = false, d_ctr = false, d_pump = false;
            for (int idx : g.second)
            {
                const Permanent& p = state.battlefield[idx];
                if (p.tapped            != a.tapped)            { d_tap  = true; }
                if (p.entered_this_turn != a.entered_this_turn) { d_sick = true; }
                if (p.damage            != a.damage)            { d_dmg  = true; }
                if (p.counters.size()   != a.counters.size())   { d_ctr  = true; }
                if (p.temp_power_bonus  != a.temp_power_bonus
                 || p.temp_tough_bonus  != a.temp_tough_bonus)  { d_pump = true; }
            }
            // Auras are NOT compared here (the link is aura->host via aura_attached_to, so it needs
            // a battlefield scan); noted so a "differs=0" is read as "none of these five differ",
            // not as "the copies are provably identical".
            TRACE("legend", "T%d %s n=%zu keep=%s tap=%d sick=%d dmg=%d ctr=%d pump=%d differs=%d",
                  state.turn_number, g.first.c_str(), g.second.size(),
                  keep == g.second.front() ? "oldest" : "other",
                  d_tap, d_sick, d_dmg, d_ctr, d_pump,
                  (d_tap || d_sick || d_dmg || d_ctr || d_pump) ? 1 : 0);
        }
        for (int idx : g.second) { if (idx != keep) { doomed.push_back(idx); } }
    }
    if (doomed.empty()) { return; }
    std::sort(doomed.begin(), doomed.end());
    // Attachments fall off a doomed legend (CR 704.5j + 301.5c): zero aura_attached_to /
    // equipped_to for anything pointing at a doomed permanent, exactly like the combat-death
    // path. Was missing for equipment -- inert under the default keep-oldest (the doomed copy is
    // the fresh, never-equipped one) but a provider keeping the newer copy would dangle the
    // attach (found during the KittyEquipment onboarding; Kemba/Balan x2 make it reachable).
    for (int idx : doomed)
    {
        const int dead_num = state.battlefield[idx].card.m_number;
        for (Permanent& e : state.battlefield)
        {
            if (e.equipped_to     == dead_num) { e.equipped_to = 0; }
            if (e.aura_attached_to == dead_num) { e.aura_attached_to = 0; }
        }
    }
    for (int idx : doomed)   // ascending -> graveyard order matches the old scan
    { state.players[state.battlefield[idx].owner_index].graveyard.push_back(state.battlefield[idx].card); }
    for (auto it = doomed.rbegin(); it != doomed.rend(); ++it)   // descending -> indices stay valid
    { state.battlefield.erase(state.battlefield.begin() + *it); }
}

// ============================================================================
// Zada / Mirrorwing solo-target trick spells (Mirrorwing Dragon deck)
// ============================================================================
// One shared resolver, called IDENTICALLY by the executor (EffectHandler custom non-permanent
// branch) and the rollout (TurnSolver::apply_one) so the copy fan-out, escalating payloads and
// token creation stay in lockstep by construction. See CardDatabase.h "Zada / Mirrorwing" block.

inline bool IsSoloTargetTrick(const CardParams& p) { return p.solo_target_trick; }

// Action::enchant_target sentinel for a solo-target trick pointed at the OPPONENT's creature.
// "Target creature" is not "target creature you control": with no own creature on the board a
// cantrip trick (Ancestral Anger / Expedite / Fists of Flame) is still castable by aiming it at
// theirs, and against the passive opponent -- who never attacks and never blocks -- the pump that
// lands there is inert, so the cast is purely "cash the card for its rider". A negative value
// (never a real m_number) keeps every existing `enchant_target > 0` aura/trick guard untouched.
inline constexpr int kTrickOpponentTarget = -1;

// A real CR-121 DRAW of n cards for `controller`: counts Player::cards_drawn_this_turn (the Fists
// of Flame pump reads it), feeds the viewer draw sink, and flags deck-out (CR 104.3c) exactly as
// ResolveDrawSpell does.
inline void TrickDraw(GameState& state, int controller, int n)
{
    if (n <= 0) { return; }
    Player& pl = state.players[controller];
    std::size_t before = pl.hand.size();
    int drew = pl.library.DrawN(n, pl.hand);
    pl.cards_drawn_this_turn += drew;
    if (drew < n) { state.player_lost_on_draw = true; }
    if (g_play_draw_sink)
    {
        for (std::size_t hi = before; hi < pl.hand.size(); ++hi)
        { g_play_draw_sink->push_back({ state.turn_number, pl.hand[hi].m_name.str() }); }
    }
}

// Treasures `controller` controls (subtype "Treasure" -- only ever the "Treasure Token" def).
inline int CountTreasuresControlled(const GameState& state, int controller)
{
    int n = 0;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index == controller && CardHasSubtype(p.card, "Treasure")) { ++n; }
    }
    return n;
}

// Create n "Treasure Token" artifact permanents (the existing cards.json token def -- its
// sac_for_mana_amount:1 makes each a live, searched SacForMana source; the Jared precedent).
inline void CreateTreasureTokens(GameState& state, int controller, int n)
{
    for (int k = 0; k < n; ++k)
    {
        Permanent token;
        token.card.m_name = "Treasure Token";
        token.card.RehashName();
        token.card.AddType(CardType::Artifact);
        token.card.m_subtypes = std::vector<std::string>{ "Treasure" };
        token.card.m_number     = state.next_token_number++;   // distinct sac source per Treasure
        token.controller_index  = controller;
        token.owner_index       = controller;
        token.entered_this_turn = true;   // artifacts have no summoning sickness; sac works now
        token.is_token          = true;
        state.battlefield.push_back(token);
    }
}

// Twinflame token: a copy of the creature at battlefield index `src_i`, except it has haste, and
// it is exiled at the beginning of the next end step (Permanent::exile_at_end, swept at both
// end-of-turn sites). Copies copy PRINTED characteristics (CR 707.2): the source's base card --
// not its counters, damage, or until-EOT effects -- which is exactly Permanent::card. m_number is
// reassigned from the token counter (two live permanents must not share a per-copy id; see
// GameState::next_token_number). The token enters through the universal enter cascade
// (FireEtbWatchers top fires the creature-enter watchers; FireOwnEtbTriggers fires a copied Goblin
// Instigator's own ETB token -- faithful, CR 707.4: the copy has the ETB trigger). A LEGENDARY
// copy (Zada) legend-rules immediately after; MirrorwingProvider::LegendKeepIndex keeps the
// original unless the hasty copy converts this turn into the win (user rule; disclosed 6a).
inline void CreateTrickCopyToken(GameState& state, int controller, int src_i)
{
    Permanent token;
    token.card          = state.battlefield[src_i].card;
    token.card.m_number = state.next_token_number++;
    token.card.AddKeyword(Keyword::Haste);
    token.controller_index  = controller;
    token.owner_index       = controller;
    token.entered_this_turn = true;
    token.is_token          = true;
    token.exile_at_end      = true;
    state.battlefield.push_back(token);
    const int idx = static_cast<int>(state.battlefield.size()) - 1;
    FireEtbWatchers(state, controller, idx);
    FireOwnEtbTriggers(state, controller, idx);
    if (state.battlefield[idx].card.HasSupertype(Supertype::Legendary))
    {
        EnforceLegendRule(state, controller);
    }
}

// Apply ONE resolution instance of a trick's payload to the creature at battlefield index `ti`
// (-1 = the untargeted "up to one target, chose none" instance: only the untargeted payloads
// resolve). Every dynamic count (cards drawn this turn, Treasures, graveyard copies) is
// recomputed HERE, per instance, so stacked copies escalate faithfully (draw first, THEN count --
// oracle order for Fists; create the Treasure, THEN count -- oracle order for Gold Rush).
inline void ApplyTrickPayload(GameState& state, int controller, const CardDefinition& def, int ti,
                              int chosen_x)
{
    const CardParams& pp = def.params;
    Player& pl = state.players[controller];

    // Untargeted riders (resolve for every instance, targeted or not).
    TrickDraw(state, controller, pp.cast_draw);
    if (pp.creates_treasures > 0) { CreateTreasureTokens(state, controller, pp.creates_treasures); }
    if (pp.cast_lifegain > 0)     { pl.life += pp.cast_lifegain; pl.life_gained_this_turn += pp.cast_lifegain; }
    if (pp.grants_extra_land_drop > 0) { pl.bonus_land_drops_this_turn += pp.grants_extra_land_drop; }

    if (ti < 0 || ti >= static_cast<int>(state.battlefield.size())) { return; }
    Permanent& tgt = state.battlefield[ti];

    if (pp.counters_on_target > 0)
    { tgt.counters.push_back(Counter{Counter::Type::PlusOnePlusOne, pp.counters_on_target}); }
    if (pp.grants_temp_haste) { tgt.temp_haste = true; }

    // P/T pump: flat + graveyard-scaled (Ancestral Anger) + drawn-count (Fists of Flame, AFTER
    // its own draw above) + Treasure-count (Gold Rush, AFTER its own Treasure above).
    int pw = pp.power_bonus;
    int tf = pp.tough_bonus;
    if (pp.gy_self_power_bonus > 0)
    {
        int copies = 0;
        for (const Card& g : pl.graveyard) { if (g.m_name == def.card.m_name) { ++copies; } }
        pw += pp.gy_self_power_bonus * (1 + copies);
    }
    if (pp.pump_per_cards_drawn_power > 0)
    { pw += pp.pump_per_cards_drawn_power * pl.cards_drawn_this_turn; }
    if (pp.pump_per_treasure_power > 0 || pp.pump_per_treasure_tough > 0)
    {
        const int tr = CountTreasuresControlled(state, controller);
        pw += pp.pump_per_treasure_power * tr;
        tf += pp.pump_per_treasure_tough * tr;
    }
    // Fortifying Draught: life gained this turn, read AFTER this instance's own cast_lifegain above
    // (gain first, THEN count -- oracle order), so a magnet fan-out escalates copy by copy.
    if (pp.pump_per_life_gained_power > 0 || pp.pump_per_life_gained_tough > 0)
    {
        const int lg = pl.life_gained_this_turn;
        pw += pp.pump_per_life_gained_power * lg;
        tf += pp.pump_per_life_gained_tough * lg;
    }
    // Luxurious Libation: the {X} PAID. Every copy of the spell copies X (CR 707.10), so unlike the
    // escalating counters above this term is the SAME for every instance of a fan-out.
    if (pp.pump_per_x_power > 0 || pp.pump_per_x_tough > 0)
    {
        pw += pp.pump_per_x_power * chosen_x;
        tf += pp.pump_per_x_tough * chosen_x;
    }
    if (pw != 0) { tgt.temp_power_bonus += pw; }
    if (tf != 0) { tgt.temp_tough_bonus += tf; }
    // DIG INSTRUMENT (MTG_TRICK_TRACE, default off): real-resolution-only dump of where a trick
    // payload landed -- target, pump, treasure count -- for diffing an executed turn against the
    // committed line's projection (the mw gi43 class: same casts, pump landed differently).
    {
        static const bool s_trick_trace = EnvOn("MTG_TRICK_TRACE");
        if (s_trick_trace && g_real_resolution)
        {
            std::fprintf(stderr,
                         "[trick] T%d %s -> %s#%d%s pw=%+d tf=%+d treasures=%d\n",
                         state.turn_number, def.card.m_name.str().c_str(),
                         tgt.card.m_name.str().c_str(), tgt.card.m_number,
                         tgt.tapped ? "(tapped)" : "", pw, tf,
                         CountTreasuresControlled(state, controller));
        }
    }

    // Luxurious Libation's token. Placed AFTER the pump deliberately -- its oracle order is
    // "gets +X/+X ... Create a ... token", the OPPOSITE of Gold Rush (token first, then count
    // Treasures), and it sits after the ti guard above so a fizzled cast (illegal target on
    // resolution, CR 608.2b) creates no token. One per resolved instance.
    if (pp.trick_token_power > 0 || pp.trick_token_toughness > 0)
    { CreateToken(state, controller, pp.trick_token_power, pp.trick_token_toughness, pp.trick_token_subtypes); }

    // Twinflame: a hasted token copy of the recipient, exiled at the beginning of the end step.
    if (pp.token_copy_of_target) { CreateTrickCopyToken(state, controller, ti); }
}

// Resolve a solo-target trick cast by `controller`. `target_number` = the chosen creature's
// card.m_number (0 = the up-to-one untargeted cast); `strive_extras` = Twinflame's searched count
// of EXTRA targets beyond the first (>0 suppresses the copy magnet -- a strived spell does not
// "target only" one creature).
//
// Copy magnet (Zada / Mirrorwing): when the single target IS a magnet, resolve one copy per OTHER
// own creature, then the original on the magnet. Copies resolve before the original (CR: they go
// on the stack above it) and their order is the controller's choice; the deterministic order used
// -- non-attack-eligible recipients first (ascending battlefield order), then eligible, magnet
// last -- is goldfish-optimal for escalating payloads (later instances see more draws/Treasures,
// so the biggest bonuses land on creatures that can actually attack). Disclosed in Stage 6a.
//
// Strive (Twinflame, strive_extras > 0): recipients = the target + the strive_extras highest
// printed-power OTHER own creatures (ties by battlefield order) -- WHICH extras is this
// deterministic resolution-time rule (disclosed), the COUNT is the searched decision.
//
// Returns false when the cast declared a target that is no longer on the battlefield (the spell
// fizzles, CR 608.2b: no payloads, straight to the graveyard). The enumeration's same-plan subset
// filter makes this unreachable in normal play; the guard keeps a stale plan safe.
inline bool ResolveSoloTargetTrick(GameState& state, int controller, const CardDefinition& def,
                                   int target_number, int strive_extras, int chosen_x)
{
    // Resolve the declared target (by stable per-copy id; deck cards AND tokens are numbered --
    // see GameState::next_token_number).
    int ti = -1;
    // kTrickOpponentTarget: "the opponent's creature" (see the sentinel's note). The passive
    // opponent's spawned bodies carry no m_number, so the target cannot be named by id -- and it
    // need not be: they are interchangeable (never attack, never block), so the sentinel resolves
    // to the lowest-index one deterministically. Reached only from the no-own-creature enumeration.
    if (target_number == kTrickOpponentTarget)
    {
        // "Target creature you control" can never resolve on their creature (CR 115.4 -- an illegal
        // target makes the spell fizzle). The enumeration no longer offers this variant for such a
        // card, so this is unreachable in a fresh plan; it guards a STALE one, the same way the
        // "declared target gone" returns below do.
        if (def.params.trick_own_target_only) { return false; }
        for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
        {
            const Permanent& p = state.battlefield[i];
            if (p.controller_index != controller && (p.card.IsCreature() || p.is_animated))
            { ti = i; break; }
        }
        if (ti < 0) { return false; }   // their creature is gone -> the spell fizzles (CR 608.2b)
        // No fan-out: it is not our magnet. chosen_x rides through exactly as the magnet path
        // below does -- pump_per_x scales an X trick the same whoever it lands on.
        ApplyTrickPayload(state, controller, def, ti, chosen_x);
        return true;
    }
    if (target_number != 0)
    {
        for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
        {
            const Permanent& p = state.battlefield[i];
            if (p.controller_index == controller && p.card.m_number == target_number
                && (p.card.IsCreature() || p.is_animated))
            { ti = i; break; }
        }
        // NAME fallback for the same-plan hand-creature target: the variant carried the number of
        // a specific HAND copy, but apply_one/cast_by_name casts an arbitrary copy of that name --
        // so match by name. Locate the referenced copy in the caster's zones (it exists somewhere)
        // to learn the name, then pick the own battlefield creature of that name, preferring an
        // attack-eligible copy (ties by battlefield order). Deterministic and shared -> lockstep.
        if (ti < 0)
        {
            const Player& pl = state.players[controller];
            std::string tgt_name;
            auto scan = [&](const std::vector<Card>& zone)
            {
                if (!tgt_name.empty()) { return; }
                for (const Card& c : zone)
                { if (c.m_number == target_number) { tgt_name = c.m_name.str(); return; } }
            };
            scan(pl.hand); scan(pl.graveyard);
            if (tgt_name.empty())
            {
                for (const Card& c : pl.library)
                { if (c.m_number == target_number) { tgt_name = c.m_name.str(); break; } }
            }
            if (!tgt_name.empty())
            {
                int fallback = -1;
                for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
                {
                    const Permanent& p = state.battlefield[i];
                    if (p.controller_index != controller || !p.card.IsCreature()
                        || p.card.m_name.str() != tgt_name) { continue; }
                    if (CanAttackFull(p, state.battlefield, controller)) { ti = i; break; }
                    if (fallback < 0) { fallback = i; }
                }
                if (ti < 0) { ti = fallback; }
            }
        }
        if (ti < 0) { return false; }   // declared target gone -> fizzle
    }

    // Human-play board-click target override (viewer feedback 2026-08-12 #2/#3): the searched
    // variant target (target_number) is only the preselected DEFAULT; the human picks off the
    // BOARD, exactly like Invigorate's own-pump prompt, and from the full RULES-legal set (every
    // own creature) -- not the provider's search-pruned candidate set (TrickTargetCandidates),
    // which is a search-breadth policy and must not bind a human. For a strive cast (Twinflame)
    // the human picks 1 + strive_extras creatures -- the count the cast already PAID for, so the
    // floor equals the ceiling (Crackle's "the count is the selection" rule); for trick_up_to_one
    // (Gold Rush / Scale the Heights) an empty pick keeps the untargeted cast. The chooser is
    // nulled by RevealLogPause in every search/rollout scope, so the autonomous engine and every
    // enumeration stay byte-identical.
    std::vector<int> strive_pick;   // human-chosen strive extras (battlefield indices), else empty
    if (g_play_target_chooser != nullptr)
    {
        std::vector<ChosenTarget> heur;
        if (ti >= 0) { heur.push_back({ 1, ti, 0 }); }
        const int extras = (ti >= 0) ? std::max(0, strive_extras) : 0;
        if (extras > 0)
        {
            // Mirror the autonomous pick below: the extras the heuristic would take (by power).
            std::vector<std::pair<int, int>> ranked;
            for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
            {
                if (i == ti) { continue; }
                const Permanent& p = state.battlefield[i];
                if (p.controller_index != controller || !p.card.IsCreature()) { continue; }
                ranked.push_back({ p.card.m_power.value_or(0), i });
            }
            std::stable_sort(ranked.begin(), ranked.end(),
                             [](const std::pair<int, int>& x, const std::pair<int, int>& y)
                             { return x.first > y.first; });
            for (int k = 0; k < extras && k < static_cast<int>(ranked.size()); ++k)
            { heur.push_back({ 1, ranked[k].second, 0 }); }
        }
        std::vector<ChosenTarget> picked =
            (*g_play_target_chooser)(state, def, controller, 1 + extras, 0, heur);
        std::vector<int> own;
        for (const ChosenTarget& c : picked)
        {
            if (c.kind != 1 || c.index < 0 || c.index >= static_cast<int>(state.battlefield.size()))
            { continue; }
            const Permanent& p = state.battlefield[c.index];
            if (p.controller_index != controller || (!p.card.IsCreature() && !p.is_animated))
            { continue; }
            own.push_back(c.index);
        }
        if (own.empty())
        {
            // Only an up-to-one trick may resolve untargeted; anything else keeps the default.
            if (def.params.trick_up_to_one) { ti = -1; }
        }
        else
        {
            ti = own.front();
            strive_pick.assign(own.begin() + 1, own.end());
        }
    }

    // Recipient list, in resolution order (see the header comment). Indices stay valid across the
    // payload applications: token/Treasure creation only push_backs (append), nothing erases
    // mid-resolution (the legend rule inside CreateTrickCopyToken erases only the just-made
    // duplicate... which sits BEHIND every recipient index -- it erases the HIGHER index of the
    // pair only when the token is not kept; when the provider keeps the token instead, recipient
    // indices after the original could shift, so KeepOldest is relied on and asserted by review).
    // FRONTLINE HEROISM: "Whenever you cast a spell that targets only a single creature you
    // control, create a 1/1 red Soldier creature token with haste, then copy that spell. The copy
    // targets that token." The qualifying cast is exactly this function's job -- a solo_target_trick
    // resolving against ONE own creature -- so the token is made HERE, before the recipient list is
    // built, and then simply joins that list: the shared payload applier below gives it the copy.
    //
    // ORDERING (controller's choice, CR 603.3b). Heroism's trigger and a magnet's trigger both go on
    // the stack above the spell; taking Heroism's FIRST is strictly better and so is what a
    // controller would do -- the Soldier exists before the magnet trigger resolves, so the magnet
    // also copies for it ("each other creature you control that the spell could target" is read on
    // the magnet trigger's resolution). Creating the token before the magnet scan below reproduces
    // that ordering exactly, and gives the Soldier one copy from Heroism plus one from the magnet.
    // Both are real: two triggered abilities, two copies.
    //
    // Only fires when the spell has a single own-creature target (ti >= 0 && strive_extras <= 0):
    // an opponent-targeted trick returned above, an untargeted trick_up_to_one has ti < 0, and a
    // strive cast targets several creatures, so none of them is "only a single creature you
    // control". Inert for every deck with no frontline_copy_tokens permanent -> byte-identical.
    // MTG_FRONTLINE_FIRST (default ON) picks WHICH order; see EngineFlags.h. ON = make the Soldiers
    // before the magnet scan below, so the magnet also fans onto them (5 instances on a Zada +
    // 2-creature board). OFF = make them after, so the magnet never sees them (4). Heroism's own
    // copy is +1 either way -- it is NOT a magnet and never fans.
    std::vector<int> frontline_tokens;
    auto make_frontline_tokens = [&]()
    {
        if (ti < 0 || strive_extras > 0) { return; }
        const int bf_before = static_cast<int>(state.battlefield.size());
        for (int i = 0; i < bf_before; ++i)
        {
            const Permanent& p = state.battlefield[i];
            if (p.controller_index != controller) { continue; }
            const CardDefinition* fd = CardDatabase::Instance().LookupCached(p.card);
            if (!fd || fd->params.frontline_copy_tokens <= 0) { continue; }
            for (int k = 0; k < fd->params.frontline_copy_tokens; ++k)
            {
                CreateToken(state, controller, fd->params.cast_token_power,
                            fd->params.cast_token_toughness, fd->params.cast_token_subtypes,
                            fd->params.created_token_color, HasteKeywords(fd->params.created_token_haste));
                frontline_tokens.push_back(static_cast<int>(state.battlefield.size()) - 1);
            }
        }
    };
    if (FrontlineTriggerFirst()) { make_frontline_tokens(); }

    std::vector<int> order;
    const bool magnet = ti >= 0 && strive_extras <= 0
        && CardDatabase::Instance().LookupCached(state.battlefield[ti].card)
        && CardDatabase::Instance().LookupCached(state.battlefield[ti].card)
               ->params.copies_solo_targeted_spells;
    if (magnet)
    {
        std::vector<int> sick, ready;
        for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
        {
            if (i == ti) { continue; }
            const Permanent& p = state.battlefield[i];
            if (p.controller_index != controller || (!p.card.IsCreature() && !p.is_animated))
            { continue; }
            (CanAttackFull(p, state.battlefield, controller) ? ready : sick).push_back(i);
        }
        order = std::move(sick);
        order.insert(order.end(), ready.begin(), ready.end());
        order.push_back(ti);   // the original resolves last (biggest escalating payload)
    }
    else if (ti >= 0 && strive_extras > 0)
    {
        order.push_back(ti);
        if (!strive_pick.empty())
        {
            // Human-chosen strive extras (the board-click override above).
            for (int i : strive_pick) { order.push_back(i); }
        }
        else
        {
            // Strive: target + the strive_extras biggest OTHER creatures by printed power.
            std::vector<std::pair<int, int>> ranked;   // (printed power, index)
            for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
            {
                if (i == ti) { continue; }
                const Permanent& p = state.battlefield[i];
                if (p.controller_index != controller || !p.card.IsCreature()) { continue; }
                ranked.push_back({ p.card.m_power.value_or(0), i });
            }
            std::stable_sort(ranked.begin(), ranked.end(),
                             [](const std::pair<int, int>& x, const std::pair<int, int>& y)
                             { return x.first > y.first; });
            for (int k = 0; k < strive_extras && k < static_cast<int>(ranked.size()); ++k)
            { order.push_back(ranked[k].second); }
        }
    }
    else if (ti >= 0) { order.push_back(ti); }
    else              { order.push_back(-1); }   // up-to-one, no target: one untargeted instance

    // Heroism's own copies, spliced in JUST BEFORE the original. Stack order says why: the magnet
    // trigger is taken second, so its copies go on the stack ON TOP of Heroism's copy and resolve
    // first -- Heroism's copy sits directly above the original spell. When there is no magnet the
    // splice lands the copy first and the original last, which is the same rule. Each Soldier gets
    // one payload per Heroism (its own copy) plus, under a magnet, one more from the magnet fan --
    // two triggered abilities, two copies, so the duplicate index is correct, not a double-count.
    if (!FrontlineTriggerFirst()) { make_frontline_tokens(); }   // magnet scan already ran -> unseen
    if (!frontline_tokens.empty() && !order.empty())
    {
        order.insert(order.end() - 1, frontline_tokens.begin(), frontline_tokens.end());
    }

    for (int i : order) { ApplyTrickPayload(state, controller, def, i, chosen_x); }
    return true;
}

// PerformEtbDig -- body in SpellEffects.cpp (see the header note above).
bool PerformEtbDig(GameState& state, int controller_index,
                          const CardParams& pp, const Permanent* self);

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

// ---- Deathrite Shaman graveyard-land fuel (gy_land_exile_mana) ---------------------------------
// Ability 1's mana tap exiles a land card from the controller's own graveyard; without one the
// source is not live. GraveyardLandFuel counts the fuel (pool builders credit at most this many
// such sources); ExileGraveyardLandForMana consumes one (deterministic first-match, both worlds)
// and returns its index for exact restore on a failed payment attempt.
inline int GraveyardLandFuel(const GameState& state, int controller)
{
    int n = 0;
    for (const Card& c : state.players[controller].graveyard) { if (ZoneCard(c).IsLand()) { ++n; } }
    return n;
}

// True when `def` is currently a live mana source w.r.t. Deathrite's graveyard-land fuel gate
// (trivially true for every other source -- byte-identical).
inline bool GraveyardFuelLive(const GameState& state, int controller, const CardDefinition& def)
{
    if (!def.params.gy_land_exile_mana) { return true; }
    return GraveyardLandFuel(state, controller) > 0;
}

// Exile (erase) the first land card in the controller's graveyard. Returns true if one was
// removed. The removed card is gone for good (no exile zone bookkeeping needed -- nothing reads
// exiled cards); callers that may FAIL the surrounding payment snapshot/restore the graveyard.
inline bool ExileGraveyardLandForMana(GameState& state, int controller)
{
    std::vector<Card>& gy = state.players[controller].graveyard;
    for (std::size_t i = 0; i < gy.size(); ++i)
    {
        if (ZoneCard(gy[i]).IsLand())
        {
            gy.erase(gy.begin() + static_cast<std::ptrdiff_t>(i));
            return true;
        }
    }
    return false;
}

// Per-permanent mana yield for a single tap. A storage-counter land (Dwarven Hold, Mercadian
// Bazaar) bursts its CURRENT storage_counters worth of {R} in one tap (0 when uncharged -> not a
// live source); every other source is its static per-tap amount. Threaded through every mana-
// accounting site (BuildPool / BuildAvailableMana / greedy tap / backtracker) so the executor,
// rollout, and planner all see the same variable burst. For non-storage sources this equals
// ManaProducedPerTap(def), so passing it everywhere is byte-identical for every non-storage deck.
inline bool CardHasSubtype(const Card& c, const std::string& sub);   // defined below (Dragonstorm helpers)

// Scaled mana DORK (Priest of Titania / Elvish Archdruid): a CREATURE whose "{T}: Add {G} for
// each <subtype>" yield is the live subtype count. Distinct from IsScaledManaLand (Three Tree
// City), which requires a positive feeder cost -- a dork's scaled tap has no feeder, so the two
// gates can never both match one card.
inline bool IsScaledManaDork(const CardDefinition& def)
{
    return def.card.IsCreature()
        && !def.params.mana_per_creature_subtype.empty()
        && def.params.mana_per_creature_feeder_generic == 0;
}

// Live yield of one scaled-dork tap: creatures matching the subtype, own side only by default,
// both sides with mana_per_creature_count_all (Priest's "each Elf on the battlefield").
inline int ScaledDorkCount(const GameState& state, int controller, const CardDefinition& def)
{
    const std::string& sub = def.params.mana_per_creature_subtype;
    int n = 0;
    for (const Permanent& q : state.battlefield)
    {
        if (!def.params.mana_per_creature_count_all && q.controller_index != controller) { continue; }
        if (q.card.IsCreature() && CardHasSubtype(q.card, sub)) { ++n; }
    }
    return n;
}

// Arbor Elf gate: a source with mana_requires_land_subtype is live only while the controller
// controls a permanent with that subtype (only Forests carry subtype "Forest"). True for every
// card without the param -> byte-identical elsewhere.
inline bool ManaSubtypeGateLive(const GameState& state, int controller, const CardDefinition& def)
{
    if (def.params.mana_requires_land_subtype.empty()) { return true; }
    return ControlsSubtype(state, controller, def.params.mana_requires_land_subtype);
}

// ---- Untap-land burst (Wirewood Lodge: "{T}: Add {C}. / {G}, {T}: Untap target Elf") -----------
// The untap ability is NOT a mana ability, but the line it enables is pure mana and fully legal at
// main-phase priority: tap a scaled Elf for N, activate the Lodge ({G} from that N + tap the
// Lodge), untap the Elf, tap it again for N -- CR 605.3 only restricts what can be activated
// INSIDE one payment window, and the engine's pay-as-you-go payment is a faithful shortcut of
// pre-floating this sequence before the cast. Because the feed pip ({G}) and the target's output
// ({G}) are the SAME colour, the whole sequence nets exactly (yield - 1) extra {G} from ONE Lodge
// tap with the Elf's tapped state unchanged -- which is how every accounting site models it (pool
// credit in AddSourceToPool, the flow oracle, the backtracker's burst branch, ManaSourceRank's
// reserve tier). Net > 0 needs yield >= 2, i.e. a SCALED dork (Priest of Titania / Elvish
// Archdruid) at 2+ Elves; below that the plain "{T}: Add {C}" mode dominates and nothing here
// fires, so the {C} is spent like any land's. USER 2026-08-20: "Lodge should tap for colourless
// when no elf taps for GG or more and also allow for tapping an existing untapped elf as needed
// ... and using the Lodge to untap it"; "allow using it for colourless early if there are no
// scaling sources at 2+ elves. Otherwise the colourless could be stranded."
// MTG_UNTAP_BURST=0 disables the whole model (the single reader lives in UntapBurstBestYield;
// every accounting site goes through these helpers). Param-gated on untap_creature_cost ->
// byte-identical for every deck without such a land.

// The single coloured pip the untap ability costs, or nullopt when the cost is not exactly one
// coloured pip -- the net-cancellation model above then does not hold, so the burst stays off.
inline std::optional<Color> UntapBurstFeedColor(const CardDefinition& def)
{
    if (!def.params.untap_creature_cost.has_value()
        || def.params.untap_creature_subtype.empty()) { return std::nullopt; }
    const ManaCost& c = *def.params.untap_creature_cost;
    if (c.generic != 0 || c.colorless != 0 || c.hybrid_count != 0 || c.has_x) { return std::nullopt; }
    if (c.white + c.blue + c.black + c.red + c.green != 1) { return std::nullopt; }
    if (c.white) { return Color::White; }
    if (c.blue)  { return Color::Blue; }
    if (c.black) { return Color::Black; }
    if (c.red)   { return Color::Red; }
    return Color::Green;
}

// Best current one-tap yield among the controller's burst-legal targets: creatures of the untap
// ability's subtype that are mana dorks producing exactly the feed colour, not summoning-sick (a
// sick Elf can neither have tapped nor re-tap, CR 302.6), and -- when `require_tapped` -- tapped
// NOW (the payment-layer form: the burst's untap must reverse a real tap at that node). The
// planner form (require_tapped=false) also counts an untapped, tappable target: it taps once
// normally first (its own credit) and the burst reverses that tap; ManaSourceRank's reserve tier
// makes the executor realise exactly that order. 0 when the model is off or nothing qualifies.
inline int UntapBurstBestYield(const GameState& state, int controller,
                               const CardDefinition& lodge_def, bool require_tapped)
{
    static const bool s_on = EnvOn("MTG_UNTAP_BURST", true);   // DEFAULT ON; =0 restores plain-{C}-only
    if (!s_on) { return 0; }
    const std::optional<Color> feed = UntapBurstFeedColor(lodge_def);
    if (!feed.has_value()) { return 0; }
    int best = 0;
    for (const Permanent& q : state.battlefield)
    {
        if (q.controller_index != controller || !q.card.IsCreature()) { continue; }
        if (require_tapped && !q.tapped) { continue; }
        if (!CanTapNow(q, state.battlefield)) { continue; }   // summoning-sick -> no tap to reverse
        if (!CardHasSubtype(q.card, lodge_def.params.untap_creature_subtype)) { continue; }
        const CardDefinition* qd = CardDatabase::Instance().LookupCached(q.card);
        if (!qd || qd->tmpl != CardTemplate::ManaDork) { continue; }
        if (qd->params.produces.size() != 1 || qd->params.produces[0] != *feed) { continue; }
        if (!ManaSubtypeGateLive(state, controller, *qd)) { continue; }
        const int y = PermanentManaYield(state, q, *qd);
        if (y > best) { best = y; }
    }
    return best;
}

// Net extra mana one Lodge tap adds via the burst OVER its own plain "{T}: Add {C}" tap:
// (best yield - 1) when the target taps for 2+, else 0. This is the PLANNER form (an untapped
// target counts -- see UntapBurstBestYield); the payment layer re-tests with require_tapped.
inline int UntapLandBurstNet(const GameState& state, int controller, const CardDefinition& def)
{
    const int y = UntapBurstBestYield(state, controller, def, /*require_tapped=*/false);
    return y >= 2 ? y - 1 : 0;
}

// Does casting this creature raise a live scaled mana dork's count (an Elf while an untapped,
// tappable Priest of Titania / Elvish Archdruid is up)? The executor half of the EnumeratePlans
// dork-growth credit keys on this: such creatures cast in their own early tier (CastOrderRank 7,
// cheapest first) so every later payment taps the dork at the higher count. False the moment no
// live scaled dork exists -> byte-identical for every other deck.
inline bool FeedsLiveScaledDork(const GameState& state, const CardDefinition& def);   // below

// --- Land Auras (Wild Growth / Fertile Ground / Overgrowth / Trace of Abundance) ---------------
// "Whenever enchanted land is tapped for mana, its controller adds an additional <X>." The bonus
// belongs to the AURA, not to the land's own CardDefinition, so unlike every other yield modifier
// in this engine it can only be found by looking at what is attached to THIS permanent. That is why
// the three yield readers below all need the Permanent, not just the def.
//
// LandAuraBonus returns the extra mana COUNT; LandAuraAddToPool credits it with the right colour.
// Both are no-ops (an early `return` on the m_number scan) for every deck with no land aura in
// play, so the whole mechanic is inert elsewhere.
inline int LandAuraBonus(const GameState& state, const Permanent& land)
{
    if (!land.card.IsLand()) { return 0; }
    int bonus = 0;
    for (const Permanent& a : state.battlefield)
    {
        if (a.aura_attached_to != land.card.m_number) { continue; }
        if (a.controller_index != land.controller_index) { continue; }
        const CardDefinition* ad = CardDatabase::Instance().LookupCached(a.card);
        if (ad && ad->params.is_land_aura) { bonus += ad->params.land_aura_extra_mana; }
    }
    return bonus;
}

// Credit each attached land aura's additional mana into `pool`, in the aura's own colour. An EMPTY
// land_aura_extra_color is "one mana of any colour" -- credited as `wild` but deliberately NOT as
// `wild_c`, because a colour cannot pay a {C} pip (ManaPool::wild_c). A named colour is credited
// exactly ({G} for Wild Growth, {G}{G} for Overgrowth).
inline void LandAuraAddToPool(ManaPool& pool, const GameState& state, const Permanent& land)
{
    if (!land.card.IsLand()) { return; }
    for (const Permanent& a : state.battlefield)
    {
        if (a.aura_attached_to != land.card.m_number) { continue; }
        if (a.controller_index != land.controller_index) { continue; }
        const CardDefinition* ad = CardDatabase::Instance().LookupCached(a.card);
        if (!ad || !ad->params.is_land_aura || ad->params.land_aura_extra_mana <= 0) { continue; }
        const std::vector<Color>& prod = ad->params.land_aura_produces;
        if (prod.empty())        { pool.wild += ad->params.land_aura_extra_mana; }
        else if (prod.size() == 1) { pool.Add(prod[0], ad->params.land_aura_extra_mana); }
        else                     { pool.wild += ad->params.land_aura_extra_mana; }
    }
}

inline int PermanentManaYield(const GameState& state, const Permanent& perm, const CardDefinition& def)
{
    if (def.params.storage_land) { return perm.storage_counters + LandAuraBonus(state, perm); }
    // Priest of Titania / Elvish Archdruid: yield = live Elf count (state-dependent).
    if (IsScaledManaDork(def))   { return ScaledDorkCount(state, perm.controller_index, def); }
    // Arbor Elf: no controlled Forest -> not a live source (yield 0; pool adds nothing).
    if (!ManaSubtypeGateLive(state, perm.controller_index, def)) { return 0; }
    // A land aura's "additional mana" rides the enchanted land's own tap, so it is part of that
    // land's per-tap yield everywhere yield is ranked (untap priority, tap order, burst nets).
    return ManaProducedPerTap(def) + LandAuraBonus(state, perm);
}

inline bool FeedsLiveScaledDork(const GameState& state, const CardDefinition& def)
{
    if (!def.card.IsCreature()) { return false; }
    const int active = state.active_player_index;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d || !IsScaledManaDork(*d)) { continue; }
        if (!CanTapNow(p, state.battlefield)) { continue; }
        if (CardHasSubtype(def.card, d->params.mana_per_creature_subtype)) { return true; }
    }
    return false;
}

// --- Three Tree City scaled mana ability ------------------------------------------------------
// "{2}, {T}: Choose a color. Add an amount of mana of that color equal to the number of creatures
// you control of the chosen type." Modelled as an activated mana ability that pays a generic feeder
// ({2}) + taps the land, then adds N mana of a SEARCH-CHOSEN single colour where N = creatures you
// control. The chosen creature TYPE is simplified to "any creature you control" (Cavern of Souls
// precedent for single-tribe decks). The basic "{T}: Add {C}" mode lives in `produces` (handled by
// the normal source path). A card is a scaled-mana land iff it declares a subtype AND a positive
// feeder cost; empty subtype -> not a scaled land, so every non-Three-Tree deck stays byte-identical.
inline bool IsScaledManaLand(const CardDefinition& def)
{
    return !def.params.mana_per_creature_subtype.empty()
        && def.params.mana_per_creature_feeder_generic > 0;
}

// N = creatures the active player controls (simplified any-creature count -- the chosen TYPE is
// collapsed to "any creature" per the analysis-goblins spec). This is the raw yield of one scaled tap.
inline int ScaledManaCreatureCount(const GameState& state)
{
    const int active = state.active_player_index;
    int n = 0;
    for (const Permanent& p : state.battlefield)
    { if (p.controller_index == active && p.card.IsCreature()) { ++n; } }
    return n;
}

// Untapped generic mana available to FEED a scaled land's activation cost, summed over the active
// player's OTHER untapped sources (the scaled land itself is tapped for the ability, so a scaled land
// never feeds itself or another scaled land -- mirrors the ramp-filter no-chain rule). A ramp filter
// has no free mode, so it is not a feeder. A crude per-source sum (enough to gate payability of {2}).
inline int ScaledManaFeederMana(const GameState& state)
{
    const int active = state.active_player_index;
    int total = 0;
    int gy_fuel = -1;   // Deathrite fuel: lazily counted, decremented per credited source
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        if (IsScaledManaLand(*d) || d->params.ramp_filter) { continue; }
        const bool is_src = (d->tmpl == CardTemplate::BasicLand)
                         || (d->tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield))
                         || d->params.mana_rock;
        if (!is_src) { continue; }
        // Deathrite: credit at most #graveyard-lands such sources.
        if (d->params.gy_land_exile_mana)
        {
            if (gy_fuel < 0) { gy_fuel = GraveyardLandFuel(state, active); }
            if (gy_fuel <= 0) { continue; }
            --gy_fuel;
        }
        total += PermanentManaYield(state, p, *d);
    }
    return total;
}

// NET mana a scaled tap adds over its basic "{T}: Add {C}" mode. The scaled ability makes N of a
// chosen colour for a {feeder} generic cost -> net (N - feeder). The basic mode already yields 1 {C},
// so scaled is only worth taking when its net beats that (net >= 1, i.e. N >= feeder + 1 -- "3+
// creatures" for the {2} feeder), and the extra colourless->colour upgrade makes net-1 strictly
// better even at the tie. Returns the net WILD mana (>= 1, colour is search-chosen) when beneficial
// AND a {feeder}-capable feeder exists, else 0 (the source falls back to its basic {C} tap).
inline int ScaledManaNetYield(const GameState& state, const CardDefinition& def)
{
    if (!IsScaledManaLand(def)) { return 0; }
    const int feeder = def.params.mana_per_creature_feeder_generic;
    const int net    = ScaledManaCreatureCount(state) - feeder;
    if (net < 1) { return 0; }                              // net <= 0 -> basic {C} is no worse
    if (ScaledManaFeederMana(state) < feeder) { return 0; } // cannot pay the {feeder} generic
    return net;
}

// A storage land is a live mana source only while charged (>= 1 counter). Non-storage sources are
// always "live" here (their usability is decided by the usual template/tap checks).
inline bool StorageSourceLive(const Permanent& perm, const CardDefinition& def)
{
    // #6: a human "hold this turn" (tap-vs-charge) makes a charged storage land not-live -> never tapped
    // for mana this turn -> stays untapped -> charges. Flag is human-play only (default false) so the
    // search/rollout and every non-storage deck are byte-identical.
    return def.params.storage_land ? (perm.storage_counters > 0 && !perm.storage_hold_this_turn) : true;
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

// Apex of Power impulse-exile: remove the top `n` cards of `controller`'s library and return them as
// STAGED cards (m_is_staged) castable until `expiry` (Apex passes turn_number = THIS turn only). Each is
// additionally flagged m_impulse_no_land so its LANDS are non-playable -- Apex says "cast SPELLS from
// among them" (a land is played, not cast, CR 601.2). The bit is set ONLY here, so Light Up / Expressive
// Iteration / Soulfire staged lands (which MUST stay playable) are unaffected. The caller decides the
// destination -- the executor (EffectHandler) wraps each in a StagedCard on Player::staged_cards (the
// AIEngine draw-breakpoint merges them into hand); the rollout (apply_one) pushes them straight into hand,
// exactly like the stages_cards DrawSpell -- so both converge on the identical in-hand staged set (lockstep).
inline std::vector<Card> DrawTopAsImpulseStaged(GameState& state, int controller, int n, int expiry)
{
    Player& ap = state.players[controller];
    std::vector<Card> out;
    for (int i = 0; i < n && !ap.library.empty(); ++i)
    {
        Card c = ap.library.DrawTop();
        c.m_is_staged       = true;
        c.m_staged_expiry   = expiry;
        c.m_impulse_no_land = true;   // this staged card's LANDS may not be PLAYED (spells only)
        out.push_back(std::move(c));
    }
    return out;
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
// Face-target sentinels shared by the multi-target damage spells (Soulfire, Crackle). A target-list
// entry >= 0 is a battlefield index; these two encode the players' faces (CRACKLE_* alias them below).
constexpr int TARGET_SELF_FACE = -1;   // the controller's own face
constexpr int TARGET_OPP_FACE  = -2;   // the opponent's face

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

// Soulfire full canonical target order = the POSITIONAL card-assignment order: opponent face, then
// you (only if life > 9 so a self-flip can't be lethal), then opponent creatures (board order), then
// your OWN creatures (expendable-first, Hinata LAST). Encoded with the shared face sentinels
// (TARGET_OPP_FACE / TARGET_SELF_FACE) and battlefield indices. The autonomous default targets a
// PREFIX of this (own creatures truncated to the searched own_targets, which -- since own creatures
// are last -- is exactly the first base+own_targets entries); human play may pick any subset of size
// >= the paid floor. The nth exiled card is always dealt to the nth target IN THIS ORDER, so the
// controller cannot steer the random flips (a killed creature still leaves the pool via SBA).
inline std::vector<int> SoulfireTargetOrder(const GameState& state, int controller)
{
    std::vector<int> out;
    out.push_back(TARGET_OPP_FACE);
    if (state.players[controller].life > 9) { out.push_back(TARGET_SELF_FACE); }
    for (int bi : SoulfireOppCreatureOrder(state, controller)) { out.push_back(bi); }
    for (int bi : SoulfireOwnCreatureOrder(state, controller)) { out.push_back(bi); }
    return out;
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
// what is removed here is steering the unknown random flip onto the best target. `def` (the Soulfire
// card definition) is used only to size the human's minimum-target FLOOR from spare mana; null (or a
// null chooser) leaves the autonomous default untouched.
inline int SpareUntappedMana(const GameState& state, int controller);   // defined below (mana helpers)
inline bool ConsumeFloatingAny(ManaPool& floating, Color& took);        // defined below (mana helpers)
// `perm` is the source PERMANENT when the caller has it (every battlefield walk does). It is needed
// only for land Auras, whose extra mana is a property of what is ATTACHED to the source rather than
// of the source's own CardDefinition -- the one yield modifier in this engine the def cannot see.
// nullptr (a source not yet on the battlefield: a rock about to be cast) -> no aura credit, which
// is exactly right, and keeps every pre-existing call byte-identical.
inline void AddSourceToPool(ManaPool& pool, const GameState& state, const CardDefinition& def,
                            int yield_override = -1, const Permanent* perm = nullptr);  // below
inline SoulfireResult SoulfireDig(GameState& state, int controller, int own_targets = 0,
                                  const CardDefinition* def = nullptr,
                                  const char* trace_side = "?")
{
    Player& ap = state.players[controller];

    // Full canonical target order (opp face, self-if-safe, opp creatures, own creatures Hinata-last).
    const std::vector<int> order = SoulfireTargetOrder(state, controller);
    // The DEFAULT (autonomous / search) target set = base (opp face + opp creatures + self-if-safe) +
    // the searched own_targets. Because own creatures are LAST in `order`, the first `default_count`
    // entries of `order` ARE that set -- byte-identical to the historical "base + first own_targets"
    // list, so the autonomous/search path below (chooser null) is unchanged.
    const int base = 1 + (ap.life > 9 ? 1 : 0)
                   + static_cast<int>(SoulfireOppCreatureOrder(state, controller).size());
    const int default_count = std::min(static_cast<int>(order.size()), base + std::max(0, own_targets));
    std::vector<int> chosen(order.begin(), order.begin() + default_count);   // autonomous default

    // Human play (claude-play): let the player target ANY subset in [min_targets, order.size()] -- the
    // opponent face, self, opponent creatures, and your own creatures are ALL individually pickable
    // (mirroring Crackle). The MINIMUM is a real MANA-affordability floor, not the base count: the cast
    // paid for `default_count` targets, with Hinata's discount CLAMPED to the spell's generic pips (you
    // can't discount the coloured {R}{R}{R}). Each fewer target loses {1} of discount = costs {1} more,
    // so the player may drop targets only as far as the SPARE untapped mana covers -- down to 1. So
    // min = max(1, effective_discount - spare_mana), where effective_discount = min(cap, default_count,
    // generic_pips). (E.g. Soulfire {6}{R}{R}{R} = 9, 6 mana out: discount clamps to 6, spare = 3 ->
    // min = 3, NOT the 7 base targets.) Picking MORE than min-required only deepens the dig / raises the
    // discount (a free over-pay). The chooser is nulled by RevealLogPause for the search/rollout, so the
    // default set stands there (byte-identical). Positional assignment follows canonical order, so we
    // re-canonicalise the player's pick.
    if (g_play_soulfire_chooser)
    {
        int discount_applied = default_count;
        if (def)
        {
            const int cap = def->params.discount_max_targets > 0 ? def->params.discount_max_targets : default_count;
            const int generic = def->card.m_mana_cost.generic;
            discount_applied = std::min(std::min(cap, default_count), generic);
        }
        const int spare = SpareUntappedMana(state, controller);
        int min_targets = std::max(1, discount_applied - spare);
        if (min_targets > default_count) { min_targets = default_count; }   // never above the paid set
        std::vector<int> pick =
            (*g_play_soulfire_chooser)(state, controller, "Soulfire Eruption", order, min_targets, chosen);
        bool ok = (static_cast<int>(pick.size()) >= min_targets)
                && (static_cast<int>(pick.size()) <= static_cast<int>(order.size()));
        std::vector<int> seen;
        for (int t : pick)
        {
            if (!ok) { break; }
            if (std::find(order.begin(), order.end(), t) == order.end()
                || std::find(seen.begin(), seen.end(), t) != seen.end()) { ok = false; break; }
            seen.push_back(t);
        }
        if (ok)
        {
            std::vector<int> ordered;
            for (int t : order) { if (std::find(pick.begin(), pick.end(), t) != pick.end()) { ordered.push_back(t); } }
            chosen = ordered;
        }
        // Charge the reduced target count's real cost: the cast already paid for `default_count`
        // targets' discount (`discount_applied`). If the human picked FEWER, each dropped target
        // restores {1} of generic cost -- tap that owed mana NOW so the line stays legal and the freed
        // mana can't be spent on another cast this turn. The floor guaranteed enough spare exists.
        // Overshoot from a >1 source (Sol Ring) is dropped, never granting extra mana (conservative).
        int real_discount = static_cast<int>(chosen.size());
        if (def)
        {
            const int cap = def->params.discount_max_targets > 0 ? def->params.discount_max_targets : default_count;
            real_discount = std::min(std::min(cap, static_cast<int>(chosen.size())), def->card.m_mana_cost.generic);
        }
        int owed = discount_applied - real_discount;
        while (owed > 0 && state.floating_mana.Total() > 0) { Color took; if (!ConsumeFloatingAny(state.floating_mana, took)) { break; } --owed; }
        for (Permanent& p : state.battlefield)
        {
            if (owed <= 0) { break; }
            if (p.controller_index != controller || p.tapped) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (!d) { continue; }
            const bool is_land = (d->tmpl == CardTemplate::BasicLand);
            const bool is_dork = (d->tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield)) || d->params.mana_rock;
            if (!is_land && !is_dork) { continue; }
            ManaPool one; AddSourceToPool(one, state, *d);
            if (one.Total() <= 0) { continue; }
            p.tapped = true;
            owed -= one.Total();   // overshoot dropped -> never grants extra mana
        }
    }

    const int n = static_cast<int>(chosen.size());

    // Dig trace (MTG_SOULFIRE_TRACE; DIAGNOSIS ONLY, no behaviour). The target COUNT is what decides
    // how many cards land in hand, and it is derived from board facts (life, opponent creatures) plus
    // the searched own_targets -- so a one-off in any of those makes the rollout and executor hold a
    // different NUMBER of cards from here on. Printed from both sides so they can be diffed.
    {
        static const bool sf_trace = EnvOn("MTG_SOULFIRE_TRACE");
        if (sf_trace)
        {
            std::string tgts;
            for (int t : chosen)
            {
                if (!tgts.empty()) { tgts += ","; }
                if (t == TARGET_OPP_FACE)       { tgts += "oppface"; }
                else if (t == TARGET_SELF_FACE) { tgts += "self"; }
                else if (t >= 0 && t < static_cast<int>(state.battlefield.size()))
                {
                    tgts += state.battlefield[t].card.m_name.str();
                    tgts += (state.battlefield[t].controller_index == controller) ? "(own)" : "(opp)";
                }
                else { tgts += "?"; }
            }
            std::string top;
            for (int i = 0; i < n && i < static_cast<int>(ap.library.size()); ++i)
            {
                if (!top.empty()) { top += ","; }
                top += ap.library[i].m_name.str();
            }
            std::fprintf(stderr,
                         "[soulfire] %-5s T%-2d search_count=%llu life=%d opp_creatures=%zu"
                         " own_targets=%d base=%d n=%d hand=%zu lib=%zu targets=[%s] exile=[%s]\n",
                         trace_side, state.turn_number,
                         static_cast<unsigned long long>(state.search_count), ap.life,
                         SoulfireOppCreatureOrder(state, controller).size(), own_targets, base, n,
                         ap.hand.size(), ap.library.size(), tgts.c_str(), top.c_str());
        }
    }

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

    // Assign cards to the CHOSEN targets POSITIONALLY (top card -> first chosen target, canonical
    // order). Creature targets take real damage; lethal ones are destroyed (SBA) AFTER all assignment
    // so later casts recompute costs from the new board and later target-dependent spells see one fewer.
    SoulfireResult r;
    std::vector<std::string> disp(mvs.size());
    std::vector<int> dead;
    for (int slot = 0; slot < n && slot < static_cast<int>(mvs.size()); ++slot)
    {
        const int t   = chosen[slot];
        const int dmg = mvs[slot];
        if (t == TARGET_OPP_FACE)
        {
            if (capture) { disp[slot] = "→ opponent face (" + std::to_string(dmg) + ")"; }
            r.face_damage = dmg;
        }
        else if (t == TARGET_SELF_FACE)
        {
            if (capture) { disp[slot] = "→ you (" + std::to_string(dmg) + ")"; }
            r.self_damage = dmg;
        }
        else if (t >= 0 && t < static_cast<int>(state.battlefield.size()))
        {
            Permanent& cre = state.battlefield[t];
            const char* who = (cre.controller_index == controller) ? "your" : "opponent";
            if (capture) { disp[slot] = "→ " + std::string(who) + " " + std::string(cre.card.m_name) + " (" + std::to_string(dmg) + ")"; }
            cre.damage += dmg;
            if (cre.damage >= cre.EffectiveToughness()) { dead.push_back(t); }
        }
    }

    std::sort(dead.begin(), dead.end(), std::greater<int>());   // descending so erase stays valid
    for (int bi : dead)
    {
        Permanent& cre = state.battlefield[bi];
        state.players[cre.owner_index].graveyard.push_back(cre.card);
        state.battlefield.erase(state.battlefield.begin() + bi);
    }

    if (capture && !flip_nums.empty())
    {
        EmitReveal(state.turn_number, "Soulfire Eruption (exiled)",
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
                         || (d->tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield))
                         || d->params.mana_rock;
        if (!is_src) { continue; }
        outs.push_back(ManaProducedPerTap(*d));
    }
    std::sort(outs.begin(), outs.end(), [](int a, int b) { return a > b; });
    int total = 0;
    for (int i = 0; i < static_cast<int>(outs.size()) && i < count; ++i) { total += outs[i]; }
    return total;
}

// The same refloat, COLOURED. RitualRefloatMana answers "how much"; this answers "of what", and it
// is what the real resolution floats. Untapping an Island and tapping it again yields BLUE, not
// "any colour" -- but the flat wild lump the refloat used to add paid a {W} pip off a mono-red
// board (the same defect Irencrag Feat's missing ritual_float_color had; user report #6).
//
// Per source, the colour rule is EXACTLY tap_source's (ManaPayment.cpp), so a refloated tap and a
// real tap produce the same pool: a single-colour source gives `amt` of its colour; a lumpy
// multi-colour source (a Karoo's 2-for-1) gives one of each colour it makes; a genuinely FLEXIBLE
// one-mana source (Forbidden Orchard, a rainbow dork) stays WILD, because there the controller
// really does choose the colour at tap time. Per-source contribution always sums to exactly
// ManaProducedPerTap, so Total() == RitualRefloatMana(state, count) by construction -- the planner's
// credit (which reads the int) is unchanged, and only the realised colours move.
// Forward decl: EffectiveProduces is defined further down (its default argument lives there, so it
// must not be repeated here -- the call below passes in_hand explicitly).
inline const std::vector<Color>& EffectiveProduces(const GameState& state, int controller,
                                                  const CardDefinition& def, bool in_hand);

// ONE per-source colour rule for every ritual float/tap-ahead contribution: a single-colour
// source gives `amt` of its colour; a lumpy multi-colour source (a Karoo's 2-for-1) gives one of
// each; a genuinely FLEXIBLE source (Forbidden Orchard, a rainbow dork) stays WILD, because there
// the controller really does choose the colour at tap time. Shared by RitualRefloatPool (the
// float model's credit) and RitualTapAheadIntoFloat (the literal model's speculative tap) so the
// two cannot drift.
// `constrain_partial_choice` (the TAP-AHEAD only): a PARTIAL-choice source (2-4 colours, one
// unit -- a Signet's U-or-R) must not float WILD, because a wild float pays ANY pip and this
// float is real spendable mana -- the ledger caught a tap-ahead Signet paying Hinata's {W}
// (§2 defect (b) re-entering through the float). The tap commits to the source's FIRST
// produces colour instead: deterministic, always legal, and the ledger's per-source credit
// covers it. A full-rainbow source keeps WILD (the controller genuinely chooses any colour).
// The FLOAT MODEL's credit keeps the old wild semantics (constrain=false) -- its GT depends
// on them and it is the model being replaced, not repaired.
inline void AddRefloatContribution(ManaPool& pool, int amt, const std::vector<Color>& prod,
                                   bool constrain_partial_choice = false)
{
    if (amt <= 0) { return; }
    if (prod.size() == 1)                { pool.Add(prod[0], amt); }
    else if (amt > 1 && prod.size() > 1)
    {
        int left = amt;
        for (Color c : prod) { if (left <= 0) { break; } pool.Add(c, 1); --left; }
        pool.wild += left;                     // more output than colours -> the rest is free choice
    }
    else if (constrain_partial_choice && prod.size() > 1 && prod.size() < 5)
    { pool.Add(prod[0], amt); }                // partial choice: commit, never launder
    else                                 { pool.wild += amt; }   // rainbow / unknown: real choice
}

inline ManaPool RitualRefloatPool(const GameState& state, int count)
{
    ManaPool pool;
    if (count <= 0) { return pool; }
    const int active = state.active_player_index;
    std::vector<std::pair<int, const CardDefinition*>> srcs;   // (per-tap output, def)
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        const bool is_src = d->card.IsLand()
                         || (d->tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield))
                         || d->params.mana_rock;
        if (!is_src) { continue; }
        srcs.emplace_back(ManaProducedPerTap(*d), d);
    }
    // Highest output first, battlefield order breaking ties (stable_sort) -- the same selection
    // RitualRefloatMana's plain descending sort makes, made deterministic in the source identity too.
    std::stable_sort(srcs.begin(), srcs.end(),
                     [](const std::pair<int, const CardDefinition*>& a,
                        const std::pair<int, const CardDefinition*>& b) { return a.first > b.first; });
    for (int i = 0; i < static_cast<int>(srcs.size()) && i < count; ++i)
    {
        const int amt = srcs[i].first;
        if (amt <= 0) { continue; }
        // Consumed immediately -- EffectiveProduces may return a thread_local buffer (reflecting /
        // domain sources), valid only until the next call.
        AddRefloatContribution(pool, amt, EffectiveProduces(state, active, *srcs[i].second, false));
    }
    return pool;
}

// MTG_SPASM_UNTAP_LITERAL -- ADOPTED DEFAULT-ON 2026-09-01 (=0 restores the float model):
// resolve an untap ritual (Reality Spasm) as a LITERAL untap of up to X TAPPED mana sources
// instead of floating the output of the board's best X sources. The float model has two proven
// defects (docs/design/reality-spasm-phase2.md §2): it ignores tapped-ness (an untapped target
// gains nothing from the real card, so whenever fewer than X sources are tapped at resolution the
// float grants mana the rules would not) and a choice-of-N dual floats WILD (pays any pip; the
// real choice is one of that source's colours). The literal untap fixes both at once because the
// untapped sources re-tap through the normal payment machinery, which prices colour choice
// exactly; paired with the tap-ahead (RitualTapAheadIntoFloat) it beat the float model in every
// hinata train cell (§9). Read ONLY here (this one function is the shared resolution for the
// executor's EffectHandler, the rollout's apply_one, ApplyPlanDirect and SubsetPayableSequential
// -> lockstep by construction, same as MTG_LEGACY_RITUAL_WILD).
inline bool SpasmUntapLiteralOn()
{
    // heurarm slot so ONE pooled batch can carry both arms of the A/B (per-job override; -1
    // unset everywhere = the env static = byte-identical off the batch path).
    static const bool env_on = EnvOn("MTG_SPASM_UNTAP_LITERAL", true);
    return heurarm::Flag(heurarm::SPASM_UNTAP_LITERAL, env_on);
}

// MTG_FILTER_FEED_STRICT -- ADOPTED DEFAULT-ON 2026-09-01 (=0 restores the lenient feed):
// the backtracker's is_filter feed pays the card's real HYBRID cost -- one of the filter's own
// colours, or a wild unit -- instead of ConsumeFloatingAny's any-unit feed. The lenient feed lets
// an off-colour float (a Sol Ring {C}) launder into two on-colour mana through Cascade Bluffs
// ("{U/R},{T}: Add ..."), which was the gi164 prepay laundering channel. Adopted after the
// full-suite adjudication (docs/design/filter-feed-strict.md): every slower game traced to
// removal of illegal credit; payment capability proven intact. Read only at the worker's
// is_filter branch, the single shared filter-feed site (the greedy path skips filters entirely and
// ManaPayment's tap_source already feeds colour-strictly), so executor and rollout are in lockstep
// by construction. The feasibility ORACLE's gross filter model is deliberately unchanged: it only
// ever claims INFEASIBLE, so its over-credit stays sound.
inline bool FilterFeedStrictOn()
{
    static const bool env_on = EnvOn("MTG_FILTER_FEED_STRICT", true);
    return heurarm::Flag(heurarm::FILTER_FEED_STRICT, env_on);
}

// MTG_FEED_FILTER_FIRST -- ADOPTED DEFAULT-ON 2026-09-01 (=0 restores the feed-blind greedy):
// feed-aware per-pip tap choice --
// the greedy rule for the stranding the strict feed exposed (docs/design/feed-aware-tap-choice.md).
// When a coloured pip's chosen DIRECT source is the LAST untapped non-filter source producing any
// of an also-candidate filter's colours (and no such colour floats), tapping it directly strands
// the filter's fed mode for the rest of the turn -- so route the SAME source through the filter
// instead (it becomes the feed): total float becomes amt+1 units instead of amt (the filter's dead
// {C} option converted into a live coloured unit), and a later cast this turn keeps its feed
// (th gi448 T3: Treasure Hunt #1 paid by Skerry's {U}{U} alone strands Bluffs and TH#2; routed
// through Bluffs the same two taps float {U}{U}{U} and TH#2 pays). Tap-ORDER preference only:
// candidates, legality and the complete backtracker fallback are unchanged (Rule 0b safe). Read
// only in TapForCostSharedOnce's scarcity selection, shared by executor and rollout -> lockstep by
// construction. Cache note (batch-pool-contamination.md rule): this changes the GREEDY only; the
// mana cache memoizes BACKTRACKER answers, which a greedy success short-circuits before reaching,
// so the lever does not enter ManaCacheKey.
inline bool FeedFilterFirstOn()
{
    static const bool env_on = EnvOn("MTG_FEED_FILTER_FIRST", true);
    return heurarm::Flag(heurarm::FEED_FILTER_FIRST, env_on);
}

// The literal resolution: untap up to `count` TAPPED mana sources, highest per-tap output first,
// battlefield order breaking ties -- the float model's selection rule (RitualRefloatPool),
// restricted to the tapped subset the real card can actually profit from. A summoning-sick dork
// is skipped exactly as the float's source predicate skips it (untapping it buys nothing this
// turn); an untapped source is not a target (it gains nothing, and choosing it would waste one
// of the X).
inline void RitualUntapSources(GameState& state, int count)
{
    if (count <= 0) { return; }
    const int active = state.active_player_index;
    std::vector<std::pair<int, int>> tapped;   // (per-tap output, battlefield index)
    for (int bi = 0; bi < static_cast<int>(state.battlefield.size()); ++bi)
    {
        const Permanent& p = state.battlefield[bi];
        if (p.controller_index != active || !p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        const bool is_src = d->card.IsLand()
                         || (d->tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield))
                         || d->params.mana_rock;
        if (!is_src) { continue; }
        tapped.emplace_back(ManaProducedPerTap(*d), bi);
    }
    std::stable_sort(tapped.begin(), tapped.end(),
                     [](const std::pair<int, int>& a, const std::pair<int, int>& b)
                     { return a.first > b.first; });
    const int n = std::min<int>(static_cast<int>(tapped.size()), count);
    for (int i = 0; i < n; ++i)
    { state.battlefield[tapped[i].second].tapped = false; }
    // Record the REAL game's untap for the prepay ledger (see GameLogger::LogUntapSources).
    // g_reveal_logger is nulled by RevealLogPause for every search/rollout/enumeration scope,
    // so only the executor's actual resolution logs -- the sim's untaps never reach the file.
    if (n > 0 && g_reveal_logger != nullptr)
    {
        std::vector<int> nums; std::vector<std::string> names;
        nums.reserve(n); names.reserve(n);
        for (int i = 0; i < n; ++i)
        {
            const Permanent& up = state.battlefield[tapped[i].second];
            nums.push_back(up.card.m_number);
            names.push_back(up.card.m_name.str());
        }
        g_reveal_logger->LogUntapSources(nums, names);
    }
}

// MTG_SPASM_UNTAP_LITERAL phase 3 -- TAP-AHEAD (docs/design/reality-spasm-phase2.md §8). Called
// immediately BEFORE an untap ritual's payment: tap every untapped, side-effect-free mana source
// into the turn-scoped float, because the ritual's resolution is about to untap up to X tapped
// sources and the enumerator always sizes X = the FULL source count -- so every source tapped
// here comes straight back up, and the float is pure profit. This is the mana-optimal way to
// cast the card (a human's "tap out, Spasm refunds the board, tap again"), and it is what makes
// the modal Spasm+payoff line payable at all: without it the untap fires on a mostly-untapped
// board and refunds ~nothing (the §8 analysis measured 97.6% of the literal model's lost games
// as exactly this gap). It also makes the enumeration credit (`a.ritual_float` = the board's
// top-X output) EXACT instead of optimistic: realised mana after the cast = float(board) +
// refreshed board = pool + HinataRitualNetBonus, the same arithmetic the planner uses.
//
// Colour per source = AddRefloatContribution (the one shared rule). Filters/signets contribute
// their engine-wide NET model (ManaProducedPerTap = 1 free) -- the same approximation the
// refloat credit and AddSourceToPool already carry. Sources whose tap has a SIDE EFFECT
// (Deathrite's graveyard exile, pain lands, storage bursts, Faeburrow's board-dependent yield,
// creature-only mana, depletion counters) are skipped: no untap-ritual deck runs one, so the
// skip is byte-inert today and safe-by-construction if that ever changes. Caps at `chosen_x`
// TOTAL tapped sources so the resolution's min(X, #tapped) always covers everything tapped here.
inline void RitualTapAheadIntoFloat(GameState& state, int chosen_x)
{
    if (!SpasmUntapLiteralOn() || chosen_x <= 0) { return; }
    const int active = state.active_player_index;
    auto is_src = [&](const Permanent& p, const CardDefinition& d)
    {
        return d.card.IsLand()
            || (d.tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield))
            || d.params.mana_rock;
    };
    int tapped_n = 0;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active || !p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d != nullptr && is_src(p, *d)) { ++tapped_n; }
    }
    for (Permanent& p : state.battlefield)
    {
        if (tapped_n >= chosen_x) { break; }
        if (p.controller_index != active || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d == nullptr || !is_src(p, *d)) { continue; }
        const CardParams& q = d->params;
        if (q.gy_land_exile_mana || q.tap_self_damage > 0 || q.tap_opponent_lifegain > 0
            || q.storage_land || q.domain_mana || q.colored_creature_only) { continue; }
        bool depletion = false;
        for (const Counter& c : p.counters)
        { if (c.type == Counter::Type::Depletion) { depletion = true; break; } }
        if (depletion) { continue; }
        if (d->tmpl == CardTemplate::ManaDork && !ManaSubtypeGateLive(state, active, *d))
        { continue; }
        p.tapped = true;
        ++tapped_n;
        // EffectiveProduces' thread_local buffer is consumed immediately, before the next call.
        // constrain_partial_choice: this float is REAL spendable mana, so a choice-limited
        // source commits to a colour here rather than floating wild (see the helper's comment).
        // The committed colour is NEED-AWARE: the hand's remaining coloured-pip demand picks it
        // (a first-listed commit measured 4 extra d0 losses incl. one unwon -- the greedy
        // committed {U} off a Signet when the hand's casts wanted {R}). Ties keep list order.
        const int amt = ManaProducedPerTap(*d);
        const std::vector<Color>& prod = EffectiveProduces(state, active, *d, false);
        if (amt == 1 && prod.size() > 1 && prod.size() < 5)
        {
            Color best = prod[0]; int best_need = -1;
            for (Color c : prod)
            {
                int need = 0;
                for (const Card& hc : state.players[active].hand)
                {
                    const ManaCost& mc = hc.m_mana_cost;
                    switch (c)
                    {
                        case Color::White: need += mc.white; break;
                        case Color::Blue:  need += mc.blue;  break;
                        case Color::Black: need += mc.black; break;
                        case Color::Red:   need += mc.red;   break;
                        case Color::Green: need += mc.green; break;
                        default: break;
                    }
                }
                if (need > best_need) { best_need = need; best = c; }
            }
            state.floating_mana.Add(best, 1);
        }
        else
        {
            AddRefloatContribution(state.floating_mana, amt, prod,
                                   /*constrain_partial_choice=*/true);
        }
    }
}

// --- Blink ("exile another target creature, then return it to the battlefield") -----------------
// Eldrazi Displacer "{2}{C}:" (any creature, returns TAPPED) and Emiel the Blessed "{3}:" (a
// creature YOU CONTROL, returns untapped). Neither cost contains {T}, so a summoning-sick outlet
// activates the turn it lands -- and neither is once-per-turn, which is what makes the deck a combo
// deck rather than a value deck.
//
// The return is a NEW OBJECT (CR 400.7): both ETB cascades fire again -- which is the engine of the
// whole deck (Peregrine Drake's "untap up to five lands" re-runs every activation, and Emiel's
// counter watcher sees another creature enter) -- and it is summoning sick again, so the blinked
// creature cannot attack this turn no matter how large Emiel makes it.
//
// m_number is PRESERVED across the blink so every id-keyed relationship stays coherent (an aura's
// aura_attached_to, an Equipment's equipped_to). Real Magic would drop those attachments when the
// permanent leaves; nothing in this deck attaches to a creature, so the two agree here -- but a
// deck that did would need the detach, so it is called out rather than assumed. Returns false if
// the target is gone (a stranded activation must not pay).
inline bool CanApplyBlink(const GameState& state, int controller, int source_id, int target_id,
                          bool own_only)
{
    bool src_ok = false, tgt_ok = false;
    for (const Permanent& p : state.battlefield)
    {
        if (p.card.m_number == source_id && p.controller_index == controller) { src_ok = true; }
        if (p.card.m_number != target_id || !p.card.IsCreature())            { continue; }
        if (own_only && p.controller_index != controller)                    { continue; }
        if (target_id == source_id)                                          { continue; }  // "another"
        tgt_ok = true;
    }
    return src_ok && tgt_ok;
}

inline void ApplyBlink(GameState& state, int controller, int source_id, int target_id,
                       bool returns_tapped)
{
    int idx = -1;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        if (state.battlefield[i].card.m_number == target_id
            && state.battlefield[i].card.IsCreature() && state.battlefield[i].card.m_number != source_id)
        { idx = i; break; }
    }
    if (idx < 0) { return; }
    const Card    raw   = state.battlefield[idx].card;
    const int     owner = state.battlefield[idx].owner_index;
    state.battlefield.erase(state.battlefield.begin() + static_cast<std::ptrdiff_t>(idx));

    const CardDefinition* d = CardDatabase::Instance().LookupCached(raw);
    Permanent perm;
    perm.card              = d ? d->card : raw;
    perm.card.m_number     = raw.m_number;
    perm.controller_index  = owner;   // "under its OWNER's control" -- we own everything in goldfish
    perm.owner_index       = owner;
    perm.entered_this_turn = true;    // a new object: summoning sick again
    perm.tapped            = returns_tapped;
    state.battlefield.push_back(perm);   // may reallocate -- no Permanent& into battlefield survives
    const int slot = static_cast<int>(state.battlefield.size()) - 1;
    FireEtbWatchers(state, owner, slot);       // Emiel's +1/+1 counter watcher lives here
    FireOwnEtbTriggers(state, owner, slot);    // the Drake's "untap up to five lands" lives here
    if (state.battlefield[slot].card.HasSupertype(Supertype::Legendary))
    { EnforceLegendRule(state, owner); }
    if (g_play_event_sink)
    {
        EmitPlayEvent(state.turn_number, "ability",
                      "\xE2\x9C\xA8 blinked " + raw.m_name.str()
                      + (returns_tapped ? " (returns tapped)" : ""));
    }
}

// Activate every affordable "{cost}, {T}: deals N damage to each opponent" source the controller
// has UNTAPPED, but only while a `keep_payable` cost stays payable afterwards. Called once per
// blink iteration (see ApplyBlinkLoop) because that is the only place the ability can fire more
// than once a turn: the loop's ETB untaps the damage land again every pass, so N iterations are N
// activations, and that is the deck's same-turn kill.
//
// Why it is safe to do without a search branch: against the single PASSIVE opponent this engine
// models, face damage is monotone -- there is no board to hold the land back for and no downside
// to the tap -- and the `keep_payable` guard means it can never starve the loop it rides on. It is
// still a RESOLUTION heuristic (the ApplyUntapCreature auto-target precedent), so it is disclosed
// in Stage 6a rather than treated as free. Returns the damage dealt.
inline ManaCost AddManaCosts(const ManaCost& a, const ManaCost& b)
{
    ManaCost c = a;
    c.generic += b.generic; c.white += b.white; c.blue  += b.blue;
    c.black   += b.black;   c.red   += b.red;   c.green += b.green;
    c.colorless += b.colorless;
    return c;
}

inline void ApplyPermAbility(GameState& state, int controller, int source_id, PermAbilityMode mode);

// Spend surplus mana on a REPEATABLE DRAIN (Essence Depleter: "{1}{C}: Target opponent loses 1 life
// and you gain 1 life"). Returns the life drained.
//
// Structurally unlike SpendSurplusOnDamageSinks, and the difference is the point. A damage sink has
// {T} in its cost, so it fires ONCE per untap and the blink loop's value is that it untaps the sink
// again each pass. A drain has no {T}: it is a pure mana sink, so the right move is to spend
// EVERYTHING at once. That is why this carries an inner loop where the damage version does not, and
// why it is the only win condition here that does not care how many times the loop untaps anything.
//
// Termination, since "repeat while payable" against unbounded mana would not stop on its own:
//   * stop once the opponent is dead -- draining a corpse is wasted mana and, worse, an unbounded
//     no-op loop inside a rollout;
//   * stop when a payment fails (the real bound in a finite-mana game);
//   * a hard cap as a backstop, so a mis-modelled free activation cannot hang a rollout the way the
//     draw-breakpoint recursion once did.
// The keep_payable guard mirrors the damage version: never spend the loop's own entry price.
inline int SpendSurplusOnDrain(GameState& state, int controller, const ManaCost& keep_payable,
                               const std::function<bool(const ManaCost&)>& pay)
{
    int drained = 0;
    std::vector<int> ids;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.drain_cost.has_value() && d->params.drain_amount > 0)
        { ids.push_back(p.card.m_number); }
    }
    if (ids.empty()) { return 0; }
    // Re-find by id before each use: pay() can tap sources AND remove permanents, so no index or
    // reference survives a payment (the same hazard SpendSurplusOnDamageSinks documents).
    constexpr int kMaxActivations = 512;
    for (int guard = 0; guard < kMaxActivations; ++guard)
    {
        if (state.players[1 - controller].life <= 0) { break; }
        bool any = false;
        for (int id : ids)
        {
            if (state.players[1 - controller].life <= 0) { break; }
            int i = -1;
            for (int bi = 0; bi < static_cast<int>(state.battlefield.size()); ++bi)
            {
                if (state.battlefield[bi].card.m_number == id
                    && state.battlefield[bi].controller_index == controller) { i = bi; break; }
            }
            if (i < 0) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(state.battlefield[i].card);
            if (!d || !d->params.drain_cost.has_value()) { continue; }
            const ManaCost c = EffectiveActivationCost(state, controller, state.battlefield[i].card,
                                                       d->params.drain_cost.value());
            // Projection check first, so a decline leaves no half-spent pool behind.
            {
                ManaPool have = AvailableManaPool(state, nullptr);
                have.AddPool(state.floating_mana);
                if (!have.CanPay(AddManaCosts(c, keep_payable))) { continue; }
            }
            if (!pay(c)) { continue; }
            ApplyPermAbility(state, controller, id, PermAbilityMode::Drain);
            drained += d->params.drain_amount;
            any = true;
        }
        if (!any) { break; }
    }
    return drained;
}

inline int SpendSurplusOnDamageSinks(GameState& state, int controller, const ManaCost& keep_payable,
                                     const std::function<bool(const ManaCost&)>& pay)
{
    int dealt = 0;
    // Collect the candidate sinks as m_numbers FIRST, then re-find each by id before using it.
    // `pay` can both tap sources AND remove permanents (a sac-for-mana Treasure is sacrificed by
    // CommitPaySacSacrifices at the end of a payment), so neither an index nor a Permanent& taken
    // across a pay() call is safe. No such source exists in this deck, which is exactly why it
    // would have gone unnoticed until some other deck picked up a damage land.
    std::vector<int> sink_ids;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller || p.tapped || !p.CanTap()) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.tap_damage_cost.has_value() && d->params.tap_damage_each_opponent > 0)
        { sink_ids.push_back(p.card.m_number); }
    }
    for (int sink : sink_ids)
    {
        int i = -1;
        for (int bi = 0; bi < static_cast<int>(state.battlefield.size()); ++bi)
        {
            if (state.battlefield[bi].card.m_number == sink
                && state.battlefield[bi].controller_index == controller) { i = bi; break; }
        }
        if (i < 0 || state.battlefield[i].tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(state.battlefield[i].card);
        if (!d || !d->params.tap_damage_cost.has_value()
            || d->params.tap_damage_each_opponent <= 0) { continue; }
        const ManaCost c = EffectiveActivationCost(state, controller, state.battlefield[i].card,
                                                   d->params.tap_damage_cost.value());
        // Never spend the loop's entry price: only fire if the board could still pay BOTH this
        // ability and the next iteration. A projection check, not a trial payment, so a decline
        // leaves no tapped land behind -- and it is the same pool the enumerator reasons over.
        {
            ManaPool have = AvailableManaPool(state, nullptr);
            have.AddPool(state.floating_mana);
            if (!have.CanPay(AddManaCosts(c, keep_payable))) { continue; }
        }
        // Pay the {T} half FIRST so the sink cannot tap itself toward its own mana cost.
        state.battlefield[i].tapped = true;
        if (!pay(c)) { SetPermTapped(state, controller, sink, false); continue; }
        const int dmg = d->params.tap_damage_each_opponent;
        state.players[1 - controller].life -= dmg;
        state.opponent_lost_life_this_turn = true;
        dealt += dmg;
        if (g_play_event_sink)
        {
            EmitPlayEvent(state.turn_number, "damage",
                          "\xF0\x9F\x94\xA5 " + state.battlefield[i].card.m_name.str()
                          + " deals " + std::to_string(dmg) + " to the opponent");
        }
    }
    return dealt;
}

// Investigate (Conservatory / Kitchen): "Create a Clue token. It's an artifact with '{2},
// Sacrifice this token: Draw a card.'" The ability lives on the "Clue Token" NAMED TOKEN DEF in
// cards.json (sac_draw_cost) exactly as a Treasure's mana ability does -- the Treasure Token
// precedent -- so the Clue is a real, searched ActivatePermAbility source rather than a bookkeeping
// counter. next_token_number keeps each Clue individually addressable.
inline void CreateClueTokens(GameState& state, int controller, int n)
{
    for (int k = 0; k < n; ++k)
    {
        Permanent token;
        token.card.m_name = "Clue Token";
        token.card.RehashName();
        token.card.AddType(CardType::Artifact);
        token.card.m_subtypes   = std::vector<std::string>{ "Clue" };
        token.card.m_number     = state.next_token_number++;
        token.controller_index  = controller;
        token.owner_index       = controller;
        token.entered_this_turn = true;   // no summoning sickness on an artifact; crackable now
        token.is_token          = true;
        state.battlefield.push_back(token);
        FireEtbWatchers(state, controller, static_cast<int>(state.battlefield.size()) - 1);
    }
}

// Is an ActivatePermAbility's source still there and still activatable? Checked BEFORE paying in
// both worlds, so a stranded activation (its source left, or it got tapped earlier in the same
// plan) is a clean no-op rather than a paid one -- the UntapCreature discipline.
inline bool PermAbilitySourceLive(const GameState& state, int controller, int source_id,
                                  PermAbilityMode mode)
{
    for (const Permanent& p : state.battlefield)
    {
        if (p.card.m_number != source_id || p.controller_index != controller) { continue; }
        // No {T} in the cost -> the source's tap state and summoning sickness are irrelevant. This
        // used to test SacDraw alone; the two Eldrazi mana sinks are the same shape, and requiring
        // an untappable source would have stopped a just-wished Essence Depleter from draining on
        // the turn it lands, and stopped an ATTACKING one from draining at all.
        if (!PermAbilityTaps(mode)) { return true; }
        return !p.tapped && p.CanTap();
    }
    return false;
}

// Set/clear the source's tapped flag by m_number. Used to pay a "{cost}, {T}" activation's TAP
// half BEFORE its mana half, which is the only ordering that stops the source paying part of its
// own cost -- a permanent taps once (CR 602.2a: the {T} symbol and a separate mana ability are the
// same single tap), so a Conservatory cannot both tap for the ability's {T} and tap for {G} toward
// its own {4}. Found by the Stage 5d sweep: Conservatory's Investigate resolved on 3 mana against a
// printed {4}, cross-checked against the life total (only one painland tap had occurred).
inline bool SetPermTapped(GameState& state, int controller, int source_id, bool tapped)
{
    for (Permanent& p : state.battlefield)
    {
        if (p.card.m_number == source_id && p.controller_index == controller)
        { p.tapped = tapped; return true; }
    }
    return false;
}

inline const char* PermAbilityLabel(PermAbilityMode mode)
{
    switch (mode)
    {
        case PermAbilityMode::TapDamage:      return "deals damage to each opponent";
        case PermAbilityMode::TapInvestigate: return "investigate";
        case PermAbilityMode::TapDraw:        return "draw a card";
        case PermAbilityMode::SacDraw:        return "sacrifice: draw a card";
        // Without these two the claude-play plan menu and the executor's LogAbility both print the
        // bare "activate" default -- a human (or the claude-play oracle) would be asked to approve
        // an unnamed ability on the deck's two win conditions.
        case PermAbilityMode::Drain:          return "target opponent loses life";
        case PermAbilityMode::ExileTop:       return "opponent exiles their top card";
        default:                              return "activate";
    }
}

// Resolve one ActivatePermAbility. The cost is already paid by the caller; this is the effect half
// only, shared by the rollout and the executor. `source_id` is the activating permanent's
// m_number.
inline void ApplyPermAbility(GameState& state, int controller, int source_id, PermAbilityMode mode)
{
    int idx = -1;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        if (state.battlefield[i].card.m_number == source_id
            && state.battlefield[i].controller_index == controller) { idx = i; break; }
    }
    if (idx < 0) { return; }
    const CardDefinition* d = CardDatabase::Instance().LookupCached(state.battlefield[idx].card);
    if (!d) { return; }
    const std::string src_name = state.battlefield[idx].card.m_name.str();

    switch (mode)
    {
        case PermAbilityMode::TapDamage:
        {
            const int dmg = d->params.tap_damage_each_opponent;
            state.players[1 - controller].life -= dmg;
            state.opponent_lost_life_this_turn = true;
            if (g_play_event_sink)
            {
                EmitPlayEvent(state.turn_number, "damage",
                              "\xF0\x9F\x94\xA5 " + src_name + " deals " + std::to_string(dmg)
                              + " to the opponent");
            }
            break;
        }
        case PermAbilityMode::TapInvestigate:
            CreateClueTokens(state, controller, 1);
            break;
        case PermAbilityMode::TapDraw:
            TrickDraw(state, controller, 1);
            break;
        case PermAbilityMode::Drain:
        {
            // "Target opponent loses N life and you gain N life." The life LOSS is the clock; the
            // gain is real but inert for a goldfish (nothing reads our life against a passive
            // opponent). Set opponent_lost_life_this_turn like TapDamage so every consumer that
            // watches for progress sees it -- life LOSS is not damage (no damage-prevention or
            // lifelink hooks), which is why this does not route through the damage path.
            const int amt = d->params.drain_amount;
            state.players[1 - controller].life -= amt;
            state.opponent_lost_life_this_turn = true;
            if (d->params.drain_self_gain > 0)
            { state.players[controller].life += d->params.drain_self_gain; }
            if (g_play_event_sink)
            {
                EmitPlayEvent(state.turn_number, "damage",
                              "\xF0\x9F\x94\xA5 " + src_name + ": opponent loses "
                              + std::to_string(amt) + " life");
            }
            break;
        }
        case PermAbilityMode::ExileTop:
        {
            // "Target opponent exiles the top card of their library. If it's a land card, you may
            // return this creature to its owner's hand." The exile goes through the shared mill
            // primitive so the deck-out is recognised the instant the zone empties (see
            // opponentdeck::TakeFromTop) -- putting that logic here instead would leave the next
            // mill card to rediscover it.
            const Card* top = state.players[1 - controller].library.size() > 0
                            ? &state.players[1 - controller].library.front() : nullptr;
            // LAND-ness comes from the DEFINITION. A card outside the battlefield carries empty
            // type masks, so `top->IsLand()` is false for every library card -- the same trap
            // ApplyRadMill documents. Read before the take, since the take invalidates the pointer.
            bool was_land = false;
            if (top != nullptr)
            {
                const CardDefinition* td = CardDatabase::Instance().LookupCached(*top);
                was_land = (td != nullptr) && td->card.IsLand();
            }
            const int took = opponentdeck::TakeFromTop(state, 1);
            if (took > 0 && g_play_event_sink)
            {
                EmitPlayEvent(state.turn_number, "exile",
                              "\xF0\x9F\x93\x9A " + src_name + ": opponent exiles their top card"
                              + std::string(was_land ? " (a land -- return declined)" : ""));
            }
            // The optional return is ALWAYS DECLINED, and `was_land` exists only to say so in the
            // log. Declining is dominant in every line this engine can realise, and decisively so
            // here: bouncing would remove the ability from the battlefield mid-loop on the ~40% of
            // activations that hit a land, which is the difference between the deck-out existing
            // and not. The param is read so the clause is visibly modelled rather than dropped.
            (void)d->params.exile_opponent_top_may_bounce_on_land;
            break;
        }
        case PermAbilityMode::SacDraw:
        {
            // "Sacrifice this token" is part of the COST, so the permanent is gone before the draw.
            state.battlefield.erase(state.battlefield.begin() + static_cast<std::ptrdiff_t>(idx));
            FireSacrificeWatchers(state, controller);   // a sac cost IS a sacrifice
            TrickDraw(state, controller, 1);
            break;
        }
        default: break;
    }
}

// Fire a repeatable {T}-less mana sink (Essence Depleter's drain, Dimensional Infiltrator's library
// exile) `want` further times, paying out of whatever mana is on the board. Returns how many
// actually fired. Shared so the executor and the rollout cannot drift.
//
// THE POINT IS THE PAYMENT COUNT, and it is a tractability fix, not a nicety. The obvious loop --
// price one activation, pay it, repeat -- costs ONE FULL PAYMENT SOLVE PER ACTIVATION, and a
// deck-out asks for up to 53 of them inside EVERY rollout node. Measured before this: single games
// took 5-70 SECONDS at d3/40ms, against well under a second for any other deck. Paying the whole
// affordable block in one solve, with a HALVING fallback when a block does not fit, turns ~53 solves
// into 1-2 on the common path and at most ~log2(53) on the failure path.
//
// Two clamps keep it honest as well as fast:
//   * `want` is capped by what the sink can still ACCOMPLISH (life remaining / cards left in the
//     opponent's library), so a 53-count plan on a 4-card library pays for 4;
//   * a HYBRID cost falls back to one-at-a-time, because scaling a hybrid pip by k is not a
//     well-formed cost (the pips are independently assignable). No such card exists today.
bool TapForCostDirect(GameState& state, const ManaCost& cost_in, bool for_creature);   // TurnSolver

inline int SpendRepeatActivations(GameState& state, int controller, int source_id,
                                  PermAbilityMode mode, const CardDefinition& def, int want)
{
    if (want <= 0) { return 0; }
    const std::optional<ManaCost>* rc = (mode == PermAbilityMode::Drain)
                                      ? &def.params.drain_cost
                                      : &def.params.exile_opponent_top_cost;
    if (!rc->has_value()) { return 0; }

    int fired = 0;
    while (want > 0)
    {
        if (OpponentHasLost(state)) { break; }
        if (!PermAbilitySourceLive(state, controller, source_id, mode)) { break; }

        // Re-priced every block: a Training Grounds can land mid-plan and change the cost.
        const ManaCost unit = EffectiveActivationCost(state, controller, def.card, rc->value());
        const int per = unit.ManaValue();
        if (per <= 0) { break; }               // a free repeatable sink would not terminate

        // Cap by what is left to DO. Beyond it the activations are pure waste, and paying for
        // waste is what makes an upper-bound count expensive.
        int useful = want;
        if (mode == PermAbilityMode::Drain && def.params.drain_amount > 0)
        {
            const int need = (std::max(1, state.players[1 - controller].life)
                              + def.params.drain_amount - 1) / def.params.drain_amount;
            useful = std::min(useful, need);
        }
        else if (mode == PermAbilityMode::ExileTop)
        {
            useful = std::min(useful, static_cast<int>(state.players[1 - controller].library.size()));
        }
        if (useful <= 0) { break; }

        ManaPool have = AvailableManaPool(state, nullptr);
        have.AddPool(state.floating_mana);
        int k = std::min(useful, static_cast<int>(have.Total()) / per);
        if (k <= 0) { break; }

        if (unit.hybrid_count > 0) { k = 1; }   // see the hybrid note above

        bool paid = false;
        while (k >= 1)
        {
            ManaCost block = unit;
            block.generic   *= k; block.white *= k; block.blue  *= k; block.black *= k;
            block.red       *= k; block.green *= k; block.colorless *= k;
            if (TapForCostDirect(state, block, /*for_creature=*/false)) { paid = true; break; }
            if (k == 1) { break; }
            k /= 2;                              // the block did not fit -- try half of it
        }
        if (!paid) { break; }

        for (int i = 0; i < k; ++i)
        { ApplyPermAbility(state, controller, source_id, mode); }
        fired += k;
        want  -= k;
    }
    return fired;
}

// Lands the caller wants back FIRST, ahead of the yield order, MOST WANTED FIRST (null = none).
// Set only by ApplyBlinkLoop.
//
// USER, 2026-09-02: "It's okay to untap by per-tap yield by default most of the time. However, we
// should have the ability to do something different when we go infinite." That is exactly the split
// this is: yield everywhere, and an explicit override for the one context where yield is the wrong
// objective. The default order is highest per-tap YIELD, which is right for mana and exactly wrong
// for Shivan Gorge -- it makes {C}, so on a board of Overgrowth'd lands it is never in the top five
// and the loop untaps it exactly never. Measured: the go-off was recognised and proposed on 708
// nodes of a 3-game sample and executed ZERO times, because one Gorge activation is all the loop
// could ever buy.
//
// The principle, stated generally so the next combo deck inherits it: ONCE MANA IS UNBOUNDED, MANA
// STOPS BEING THE OBJECTIVE. A land you can convert into damage, cards, or any other progress is
// worth more than the one or two extra mana the land it displaces would make, because the loop
// already has an unbounded supply of the thing yield measures. So this is an ORDERED SET rather
// than the single id it started as: the loop promotes every permanent it can cash out, damage
// sinks first (they end the game) and draw/investigate sinks behind them (they find the card that
// does). A one-sink version silently hardcoded "the sink is Shivan Gorge", which is a deck fact,
// not an engine one.
//
// Scoped, thread_local, and nulled outside the loop, so every other untap keeps the yield order.
inline thread_local const std::vector<int>* g_etb_untap_priority = nullptr;

struct EtbUntapPriorityScope
{
    const std::vector<int>* prev;
    explicit EtbUntapPriorityScope(const std::vector<int>* v)
        : prev(g_etb_untap_priority) { g_etb_untap_priority = v; }
    ~EtbUntapPriorityScope() { g_etb_untap_priority = prev; }
};

// Rank of `number` in the priority set: 0 = most wanted, -1 = not in it.
inline int EtbUntapPriorityRank(int number)
{
    if (g_etb_untap_priority == nullptr) { return -1; }
    for (std::size_t i = 0; i < g_etb_untap_priority->size(); ++i)
    { if ((*g_etb_untap_priority)[i] == number) { return static_cast<int>(i); } }
    return -1;
}

// The blink loop -- ONE shared driver both worlds call, so the executor realises exactly the line
// the rollout scored. Per iteration, in this order:
//   1. TAP AHEAD, if the blinked creature untaps lands: its ETB is about to refresh up to N lands,
//      so tapping N into the float first turns the refund into spendable mana (the Reality Spasm
//      manoeuvre; without it the loop refunds an already-untapped board and nets nothing).
//   2. Fire the damage sinks (Shivan Gorge), guarded so the next iteration stays payable. This is
//      the only place a "{T}" ability can fire repeatedly in a turn, because step 4 untaps it.
//   3. Pay ONE activation, re-costed against the board (a Training Grounds could have entered
//      mid-plan; the Equip precedent says recompute at the pay site so payment is never wrong).
//   4. Blink: the target leaves and re-enters, re-firing both ETB cascades.
// BREAKS the moment an iteration cannot be paid or the target is gone -- so an over-large K from
// the provider costs nothing but a loop that stops early.
inline int ApplyBlinkLoop(GameState& state, int controller, int source_id, int target_id,
                          const CardParams& outlet, int iterations,
                          const std::function<bool(const ManaCost&)>& pay)
{
    if (!outlet.blink_cost.has_value()) { return 0; }
    // The sinks this loop wants back every iteration, MOST WANTED FIRST. Fixed for the whole loop:
    // lands do not move, so it need not be recomputed per iteration.
    //
    // DAMAGE SINKS ONLY, and that restriction is MEASURED, not assumed. Promoting draw/investigate
    // sinks too (Mariposa's {5},{T}: draw; Kitchen's Investigate) is the obvious generalisation and
    // it LOSES: +0.0437 avg win turns, t +5.99, 8 of 8 seeds worse, paired 800 games a side.
    //
    // The mechanism explains the sign. A promotion is only worth its displaced yield if the loop can
    // CASH the promoted permanent, and the loop's per-iteration spend is SpendSurplusOnDamageSinks --
    // damage and nothing else. So untapping a draw land instead of an Overgrowth'd one converts real
    // loop mana into an ability no iteration ever activates: pure loss. Damage sinks are the whole
    // exception because firing one IS the win condition.
    //
    // So the ordered-set shape above is the capability ("do something different when we go
    // infinite"), and this is the policy that currently earns its place in it. Adding a class of
    // sink here is only correct alongside a matching per-iteration SPEND for it -- do both, or
    // neither. Empty on every deck without a damage land, which keeps this byte-identical elsewhere.
    // ONE sink, not all of them -- the `break` is deliberate and is what makes this generalisation
    // byte-identical to the single-id version it replaces. Promoting every copy was measured too
    // (Shivan Gorge is a 4-of, so it is a real alternative): it buys damage per iteration but spends
    // the untap slots that would have gone to high-yield lands, and it came out a WASH -- 7.2200 vs
    // 7.2238 over 8 seeds x 100. A wash is not a reason to move off the shipped behaviour, so the
    // set carries one entry today and the ordering machinery is simply ready for the day a deck has
    // two DIFFERENT sinks worth ranking against each other.
    std::vector<int> sinks;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.tap_damage_cost.has_value() && d->params.tap_damage_each_opponent > 0)
        { sinks.push_back(p.card.m_number); break; }
    }
    int done = 0;
    for (int k = 0; k < iterations; ++k)
    {
        if (!CanApplyBlink(state, controller, source_id, target_id, outlet.blink_own_only)) { break; }
        const Card* src_card = nullptr;
        const CardDefinition* tgt_def = nullptr;
        for (const Permanent& p : state.battlefield)
        {
            if (p.card.m_number == source_id) { src_card = &p.card; }
            if (p.card.m_number == target_id) { tgt_def = CardDatabase::Instance().LookupCached(p.card); }
        }
        if (src_card == nullptr) { break; }
        const Card src_copy = *src_card;   // ApplyBlink push_backs; no battlefield pointer survives
        const int untaps = (tgt_def != nullptr) ? tgt_def->params.etb_untap_lands : 0;
        const ManaCost c = EffectiveActivationCost(state, controller, src_copy,
                                                   outlet.blink_cost.value());
        // ORDER MATTERS. The sink fires BEFORE the tap-ahead: the tap-ahead would otherwise tap the
        // Gorge for its one {C} and the damage ability would find it already tapped -- turning the
        // deck's kill into a rounding error on the mana. Firing it first also means the Gorge is
        // tapped when the ETB untap runs, which is what puts it back for the next iteration.
        SpendSurplusOnDamageSinks(state, controller, c, pay);
        if (untaps > 0) { EtbUntapTapAheadIntoFloat(state, controller, untaps); }
        if (!pay(c)) { break; }
        // Emiel's optional {G/W} counter trigger fires inside ApplyBlink's ETB cascade; hand it this
        // loop's payer so its mana is on the same books as the activation's (see
        // PayOptionalTriggerCost). Restored on scope exit. The untap priority puts the damage sink
        // back at the front of the untapped set (see g_etb_untap_priority).
        //
        // PAY IT ON THE LAST ITERATION ONLY (user, 2026-09-02: "this is silly to do when we plan to
        // flicker the creature, when we are going off being the biggest example"). This is not a
        // preference, it is arithmetic: CR 400.7 makes the returned permanent a NEW OBJECT, so
        // iteration k+1 WIPES the counter iteration k paid for. Paying every pass buys exactly one
        // surviving counter for K payments -- and, far worse, {G/W} per iteration is subtracted from
        // the loop's PER-ITERATION MARGIN. A loop netting +1 mana a pass is unbounded; the same loop
        // paying {G/W} every pass nets 0 and is not a combo at all. So the always-pay heuristic could
        // silently un-make the deck's win condition.
        //
        // A DECLINING payer is installed for the other passes rather than a null one: null would
        // fall through to the turn-scoped float in PayOptionalTriggerCost and pay anyway, which is
        // the same bug wearing a different hat.
        {
            const bool last_pass = (k + 1 == iterations);
            const std::function<bool(const ManaCost&)> decline =
                [](const ManaCost&) { return false; };
            EtbOptionalPayerScope   _eops(last_pass ? &pay : &decline);
            EtbUntapPriorityScope   _eups(sinks.empty() ? nullptr : &sinks);
            ApplyBlink(state, controller, source_id, target_id, outlet.blink_returns_tapped);
        }
        ++done;
    }
    // One last damage-sink pass with nothing held back: the loop is over, so there is no next
    // iteration to keep payable and any float left is genuinely surplus. Only after a loop that
    // actually ran -- a blink that never fired must not quietly tap a land for damage the plan
    // did not ask for (the Gorge has its own ActivatePermAbility action for that).
    if (done > 0) { SpendSurplusOnDamageSinks(state, controller, ManaCost{}, pay); }
    return done;
}

// --- Training Grounds: static cost reduction for ACTIVATED abilities ---------------------------
// "Activated abilities of creatures you control cost {2} less to activate. This effect can't reduce
// the mana in that cost to less than one mana."
//
// A different axis from every reducer in EffectiveSpellCost, all of which are keyed on the card
// being CAST; nothing in this engine reduced an ACTIVATION cost before. Two consequences worth
// naming:
//   * The FLOOR is one mana, not zero. Every spell reducer floors the generic half at 0 and stops
//     there; this one must also refuse to zero out a cost that is ALL generic. {3} -> {1}.
//   * It reduces the GENERIC half only (CR 601.2f applies reductions to generic first and there is
//     no "reduce a coloured pip" here), so {2}{C} -> {C}: the generic 2 goes, the colourless pip
//     stays and still demands real colourless mana. That is the whole reason Displacer's activation
//     stays gated on a {C} source even with two Training Grounds out.
// Must be applied at EVERY site an activation cost is read -- enumeration, the rollout pay, the
// executor pay, and the human trial-pay -- or the planner prices a line the payer cannot buy (or
// vice versa). Returns `printed` unchanged when no reducer is out, so every other deck is
// byte-identical.
inline ManaCost EffectiveActivationCost(const GameState& state, int controller,
                                        const Card& source, const ManaCost& printed)
{
    if (!source.IsCreature()) { return printed; }   // "abilities of CREATURES you control"
    int reduce = 0;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d) { reduce += d->params.reduces_creature_activation; }
    }
    if (reduce <= 0) { return printed; }
    ManaCost c = printed;
    // Pips that are NOT generic survive the reduction and already satisfy the one-mana floor; only
    // an all-generic cost has to keep a mana behind.
    const int pips = c.white + c.blue + c.black + c.red + c.green + c.colorless;
    const int floor_generic = (pips >= 1) ? 0 : 1;
    c.generic = std::max(floor_generic, c.generic - reduce);
    return c;
}

// --- "When this creature enters, untap up to N lands" (Peregrine Drake 5, Cloud of Faeries 2) ----
// A REAL untap of LANDS you control (not the ritual's older floating-mana fake), highest per-tap
// yield first -- and per-tap yield now includes any land Aura on the host, so an Overgrowth'd land
// is untapped ahead of a bare one, which is what a human does. Ties keep battlefield order, so the
// executor and the rollout pick the identical set.
//
// "Up to" N is always taken in full: an untapped land is never worse than a tapped one for a
// goldfish (no opponent to bluff, nothing punishes an open board), so the choice collapses -- the
// only real decision is WHICH lands, which the yield order settles. Disclosed in Stage 6a.
inline void EtbUntapLands(GameState& state, int controller, int count)
{
    if (count <= 0) { return; }
    std::vector<std::pair<int, int>> tapped;   // (sort key, battlefield index)
    for (int bi = 0; bi < static_cast<int>(state.battlefield.size()); ++bi)
    {
        const Permanent& p = state.battlefield[bi];
        if (p.controller_index != controller || !p.tapped || !p.card.IsLand()) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        int key = PermanentManaYield(state, p, *d);
        // Above any real yield, so a wanted sink is always in the untapped set; the rank keeps the
        // caller's own ordering among them (damage before draw) while still leaving every
        // non-priority land ranked by yield below the whole set.
        const int rank = EtbUntapPriorityRank(p.card.m_number);
        if (rank >= 0) { key += 1000000 - 1000 * rank; }
        tapped.emplace_back(key, bi);
    }
    std::stable_sort(tapped.begin(), tapped.end(),
                     [](const std::pair<int, int>& a, const std::pair<int, int>& b)
                     { return a.first > b.first; });
    const int n = std::min<int>(static_cast<int>(tapped.size()), count);
    for (int i = 0; i < n; ++i) { state.battlefield[tapped[i].second].tapped = false; }
    // Real-game untap ledger for the viewer (see RitualUntapSources' identical block): g_reveal_logger
    // is nulled by RevealLogPause for every search/rollout scope, so only the executor logs.
    if (n > 0 && g_reveal_logger != nullptr)
    {
        std::vector<int> nums; std::vector<std::string> names;
        nums.reserve(n); names.reserve(n);
        for (int i = 0; i < n; ++i)
        {
            const Permanent& up = state.battlefield[tapped[i].second];
            nums.push_back(up.card.m_number);
            names.push_back(up.card.m_name.str());
        }
        g_reveal_logger->LogUntapSources(nums, names);
    }
}

// Enumeration credit for the above: the mana the untap will hand back. Sized as the top-N per-tap
// yields over the controller's lands REGARDLESS of tapped-ness, because at enumeration time the
// lands that will fund this very cast are still untapped -- they are tapped moments later by the
// tap-ahead. An upper bound (the executor realises min over what is actually tapped at resolution),
// exactly as a.ritual_float is under MTG_SPASM_UNTAP_LITERAL: an over-credited plan's unpayable
// follow-up is dropped by the pay path and scores honestly, so the cost is plan-ranking quality,
// never phantom mana.
inline int EtbUntapLandsCredit(const GameState& state, int count)
{
    if (count <= 0) { return 0; }
    const int active = state.active_player_index;
    std::vector<int> yields;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active || !p.card.IsLand()) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        yields.push_back(PermanentManaYield(state, p, *d));
    }
    std::sort(yields.begin(), yields.end(), std::greater<int>());
    int total = 0;
    for (int i = 0; i < static_cast<int>(yields.size()) && i < count; ++i) { total += yields[i]; }
    return total;
}

// TAP-AHEAD for an ETB-untap creature -- the same manoeuvre RitualTapAheadIntoFloat performs for
// Reality Spasm, and for the same reason: the cast is about to untap up to N lands, so every land
// tapped BEFORE paying comes straight back. Without it the Drake resolves onto a mostly-untapped
// board and refunds nothing, which is the difference between "free 2/3 flier that refills your
// mana" and "five-mana 2/3". Bounded by `count` (never tap more than the untap will restore), and
// it deliberately skips sources whose tap has a SIDE EFFECT (pain, drip, depletion, storage) --
// those are not free to cycle. Called immediately before the cast's payment in all four worlds.
inline void EtbUntapTapAheadIntoFloat(GameState& state, int controller, int count)
{
    if (count <= 0) { return; }
    int tapped_n = 0;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index == controller && p.tapped && p.card.IsLand()) { ++tapped_n; }
    }
    for (Permanent& p : state.battlefield)
    {
        if (tapped_n >= count) { break; }
        if (p.controller_index != controller || p.tapped || !p.card.IsLand()) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d == nullptr) { continue; }
        const CardParams& q = d->params;
        if (q.gy_land_exile_mana || q.tap_self_damage > 0 || q.tap_opponent_lifegain > 0
            || q.storage_land || q.domain_mana || q.colored_creature_only || q.is_filter
            || q.ramp_filter) { continue; }
        bool depletion = false;
        for (const Counter& c : p.counters)
        { if (c.type == Counter::Type::Depletion) { depletion = true; break; } }
        if (depletion) { continue; }
        p.tapped = true;
        ++tapped_n;
        // COMMIT A COLOUR for a choice-limited source, exactly as RitualTapAheadIntoFloat does and
        // for the same reason: this float is REAL spendable mana, and AddSourceToPool books a
        // multi-colour land as `wild`, which pays ANY pip. Floating it wild is free colour-fixing.
        //
        // The Stage 5d sweep caught it being exploited: a turn where the tap-ahead left
        // floating_mana {G:1, wild:2} let Emiel the Blessed resolve its {W}{W} with no white source
        // ever tapped for it. The committed colour is NEED-AWARE (the hand's remaining coloured-pip
        // demand picks it); ties keep list order.
        const int amt = ManaProducedPerTap(*d);
        const std::vector<Color>& prod = EffectiveProduces(state, controller, *d, false);
        if (amt == 1 && prod.size() > 1 && prod.size() < 5)
        {
            Color best = prod[0]; int best_need = -1;
            for (Color c : prod)
            {
                int need = 0;
                for (const Card& hc : state.players[controller].hand)
                {
                    const ManaCost& mc = hc.m_mana_cost;
                    switch (c)
                    {
                        case Color::White: need += mc.white; break;
                        case Color::Blue:  need += mc.blue;  break;
                        case Color::Black: need += mc.black; break;
                        case Color::Red:   need += mc.red;   break;
                        case Color::Green: need += mc.green; break;
                        default: break;
                    }
                }
                if (need > best_need) { best_need = need; best = c; }
            }
            state.floating_mana.Add(best, 1);
        }
        else
        {
            AddRefloatContribution(state.floating_mana, amt, prod,
                                   /*constrain_partial_choice=*/true);
        }
        // A land Aura's bonus rides the same tap and keeps its own colour (never wild unless the
        // aura itself says "any colour").
        if (LandAuraBonus(state, p) > 0) { LandAuraAddToPool(state.floating_mana, state, p); }
    }
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
        if (d->card.IsLand() || (d->tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield)) || d->params.mana_rock) { ++n; }
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
// `copies` (default 1) is the Desperate Ritual SPLICE multiplier: casting ONE base while splicing k
// OTHER copies adds (k+1) times the per-copy float. Threaded so enum / rollout / executor scale the
// float IDENTICALLY off the same k (lockstep). copies==1 for every non-spliced ritual -> byte-identical.
// Only Desperate Ritual is spliceable (no ritual_float_gy_self_bonus), so the per-copy amount is a flat
// multiply; Rite of Flame's graveyard bonus is never spliced (copies always 1) and stays intact.
inline int RitualFloatAmount(const GameState& state, const CardDefinition& def, int chosen_x, int copies = 1)
{
    if (def.params.untap_x_mana_sources) { return RitualRefloatMana(state, chosen_x) * copies; }
    if (def.params.ritual_floating_mana > 0)
    {
        int amt = def.params.ritual_floating_mana;
        if (def.params.ritual_float_gy_self_bonus)
        {
            // Rite of Flame: +1 per card with THIS name in any graveyard, counted at resolution so a
            // same-turn chain escalates (each prior copy is already in the graveyard when the next resolves).
            for (const Player& pl : state.players)
            {
                for (const Card& c : pl.graveyard)
                {
                    if (c.m_name == def.card.m_name) { ++amt; }
                }
            }
        }
        return amt * copies;
    }
    return 0;
}

// Reusable "add `amt` mana of ONE chosen colour to the turn-scoped reserve" dimension. `col` is a
// one-letter colour ("W"/"U"/"B"/"R"/"G"/"C"); EMPTY = WILD (any single pip). This is the single
// colour->field switch shared by BOTH the ritual float (ApplyRitualFloat) and the chosen-colour
// floats -- Lotus Bloom's SacForMana and (next) Apex of Power's "add ten of one colour" -- so all
// three route colour identically and stay lockstep. Floats (not a literal tap): the planner credits
// the same gross amount, so the search's projected combo and the executed combo never diverge.
// Add `amt` mana of the named colour ("" / unknown = wild) to ANY pool. Split out of
// AddChosenColorFloat so the cast-order range ladder can credit a producer's output into its
// projection pool with the same colour semantics the real float uses, rather than a second copy
// of this switch that would drift the moment a colour is added.
inline void AddColorToPool(ManaPool& pool, const std::string& col, int amt)
{
    if (amt <= 0) { return; }
    if (col.empty())      { pool.wild      += amt; }
    else if (col == "W")  { pool.white     += amt; }
    else if (col == "U")  { pool.blue      += amt; }
    else if (col == "B")  { pool.black     += amt; }
    else if (col == "R")  { pool.red       += amt; }
    else if (col == "G")  { pool.green     += amt; }
    else if (col == "C")  { pool.colorless += amt; }
    else                  { pool.wild      += amt; }  // unknown -> wild
}

inline void AddChosenColorFloat(GameState& state, const std::string& col, int amt)
{
    AddColorToPool(state.floating_mana, col, amt);
}

// Apply a ritual's floating mana ON RESOLUTION (shared by EffectHandler -- the real executor --
// and the rollout's apply_one). Adds the gross float to the turn-scoped reserve (state.floating_mana)
// so a later same-turn cast (Crackle) can spend it. Default (empty ritual_float_color) = WILD,
// preserving byte-identity for Irencrag Feat / Reality Spasm; a specific colour (the Dragonstorm
// rituals set "R") floats real coloured mana that cannot pay off-colour pips. No-op for non-ritual cards.
// `copies` (default 1) = Desperate Ritual splice multiplier (splice_count+1); passed through to
// RitualFloatAmount so the resolved float scales by (k+1) in lockstep across all cast paths.
inline void ApplyRitualFloat(GameState& state, const CardDefinition& def, int chosen_x, int copies = 1)
{
    // An UNTAP ritual (Reality Spasm) refloats the output of the sources it untaps, so its colours are
    // those sources' colours -- see RitualRefloatPool. A FIXED burst names its colour on the card
    // (ritual_float_color); every ritual in the database now sets one, so the empty-string WILD
    // fallback in AddColorToPool is a guard, not a mode any real card takes.
    //
    // MTG_LEGACY_RITUAL_WILD=1 restores the PRE-FIX behaviour (every ritual floats WILD, which pays
    // any coloured pip) for A/B ATTRIBUTION only -- it is how a ground-truth game that moved is tied
    // to this fix rather than to another change in the same batch. It re-enables an illegal payment;
    // never run a measurement on it.
    // AUDIT (MTG_WILD_PIP_AUDIT): mana a ritual floats with NO colour, which then pays any coloured
    // pip -- exactly Irencrag Feat's seven "red" and Reality Spasm's untap. Counted on BOTH paths,
    // so the same meter sizes the old hole (fixes off) and asserts it is shut (fixes on, = 0).
    static const bool s_legacy_wild = EnvOn("MTG_LEGACY_RITUAL_WILD");
    if (WildPipAuditOn())
    {
        const int amt = RitualFloatAmount(state, def, chosen_x, copies);
        const bool uncolored = s_legacy_wild
                             || (!def.params.untap_x_mana_sources
                                 && def.params.ritual_float_color.empty());
        if (amt > 0 && uncolored)
        { g_ritual_uncolored_float.fetch_add(amt, std::memory_order_relaxed); }
    }
    if (s_legacy_wild)
    {
        // Exactly the pre-fix line: one colourless-agnostic WILD lump, ignoring ritual_float_color
        // (RitualFloatAmount already handles the untap case via RitualRefloatMana).
        AddChosenColorFloat(state, std::string(), RitualFloatAmount(state, def, chosen_x, copies));
        return;
    }
    if (def.params.untap_x_mana_sources)
    {
        if (SpasmUntapLiteralOn())
        {
            // The literal model (see the reader's comment above): untap up to X tapped sources and
            // let the payment machinery re-tap them -- no float at all. Every caller of this
            // function (executor, rollout apply, plan apply, sequential feasibility) takes this
            // same branch, so all four worlds realise the identical state change. NOTE the
            // planner's enumeration credit (a.ritual_float, stamped from RitualFloatAmount) is now
            // an OPTIMISTIC upper bound under this flag -- an over-credited plan's unpayable cast
            // is dropped by the rollout's TapForCostDirect failure path and scores honestly, so
            // the cost is plan-ranking quality, never phantom mana.
            for (int c = 0; c < copies; ++c) { RitualUntapSources(state, chosen_x); }   // copies==1 in practice
            return;
        }
        ManaPool add = RitualRefloatPool(state, chosen_x);
        for (int c = 1; c < copies; ++c) { state.floating_mana.AddPool(add); }   // copies==1 in practice
        state.floating_mana.AddPool(add);
        return;
    }
    AddChosenColorFloat(state, def.params.ritual_float_color, RitualFloatAmount(state, def, chosen_x, copies));
}

// Forward decl: HinataInPlay (defined just below). Used by HinataRitualNetBonus.
inline bool HinataInPlay(const GameState& state);

// NET floating mana the active player's in-hand rituals would add THIS turn = gross float minus
// the ritual's own cast cost, assuming Hinata is in play (so an untap ritual's {X} is free). The
// planner adds this to a payoff X-spell's (Crackle's) affordable pool so its max X reaches the
// combo's lethal value. 0 with no Hinata or no ritual in hand -> the X-enum is unchanged for
// every non-Hinata deck. (Gross is credited per-subset in Solve::consider; the base cost lives
// in `combined` there, so pool+gross-cost == pool+net == this -- exact, conservative.)
inline int HinataRitualNetBonus(const GameState& state, bool assume_hinata = false)
{
    const Player& ap = state.ActivePlayer();
    const bool hinata = HinataInPlay(state) || assume_hinata;
    int net = 0;
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d || !IsManaRitual(*d)) { continue; }
        // An untap ritual (Reality Spasm) only NETS mana when Hinata makes its {X} free; without
        // her, untapping X costs X mana to recast (break-even). A fixed burst (Irencrag) nets
        // always. `assume_hinata` (MTG_HINATA_SUBSET_CREDIT): she is castable from hand this
        // turn, so size the range as if she resolves first -- the subset gate validates.
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
    // NOTE: for Magma Opus the faithful, committed-face discount is now owned by the Hinata provider
    // (ScaledCastVariants) and priced per searched face level; this generic over-count ("2 + every
    // permanent") is the fallback the engine uses only when that model is off -> byte-identical default.
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
//
// `crackle_targets` (default -1) is the DECLARED count of extra beneficial targets beyond the
// opponent face for a discount_targets_scale_x spell (Crackle). When >= 0 the discount DERIVES from
// the declaration -- min(X, 1 + count) (the face plus `count` extras, each {1}) -- so a searched/
// human-chosen target set drives the reduction and the extras are then faithfully damaged
// (CrackleHitExtraTargets). When < 0 (every legacy caller, and non-scale_x spells) it falls back to
// the old auto-max min(X, avail), so behaviour is byte-identical wherever the count isn't threaded.
// The InPlay-UNGATED body, split out for the SAME-SUBSET credit (MTG_HINATA_SUBSET_CREDIT): the
// enumerator needs "what WOULD her discount be" for a plan that casts her itself, where
// HinataInPlay is still false at the enumeration state. Target availability is computed on the
// CURRENT board (she is not yet a target herself), which under-counts by at most 1 -- the
// conservative direction for a credit. Every other caller goes through the gated wrapper below.
inline int HinataGenericDiscountAssumed(const CardDefinition& def, const GameState& state,
                                        int chosen_x, int crackle_targets = -1)
{
    if (def.params.discount_targets_scale_x)
    {
        if (chosen_x <= 0) { return 0; }
        if (crackle_targets >= 0)
        {
            int disc = 1 + crackle_targets;                 // face + declared extras
            return disc < chosen_x ? disc : chosen_x;       // capped at X (can't have > X targets)
        }
        int avail = HinataAvailableTargets(def, state);     // auto-max fallback (unchanged)
        return chosen_x < avail ? chosen_x : avail;
    }
    int cap = def.params.discount_max_targets;
    if (cap <= 0) { return 0; }
    int avail = HinataAvailableTargets(def, state);
    return cap < avail ? cap : avail;
}

inline int HinataGenericDiscount(const CardDefinition& def, const GameState& state, int chosen_x,
                                 int crackle_targets = -1)
{
    if (!HinataInPlay(state)) { return 0; }
    return HinataGenericDiscountAssumed(def, state, chosen_x, crackle_targets);
}

// Is the reducer CASTABLE from the active player's hand this turn (in hand + the board's source
// count covers her printed cost)? The gate for every "assume Hinata resolves first" emission /
// credit below: cheap (one hand scan + the cached source count), and deliberately OPTIMISTIC on
// colours -- the subset gate's CanPay and the per-cast apply both validate for real.
inline bool HinataCastableFromHand(const GameState& state)
{
    const Player& ap = state.ActivePlayer();
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (d && d->params.hinata_cost_reducer)
        { return ManaSourceCount(state) >= d->card.m_mana_cost.ManaValue(); }
    }
    return false;
}

// True for Crackle with Power specifically: a scale_x Hinata-discount spell that DEALS damage to
// its targets (x_damage_multiplier > 0). This is the spell that uses the declared-count model
// (crackle_targets -> derived discount + faithful 5X damage). It deliberately EXCLUDES Reality
// Spasm, which is also discount_targets_scale_x but UNTAPS permanents (no damage) and keeps the
// auto-max discount -- so gating the count model on this predicate leaves Reality Spasm's cost
// (and every other spell) byte-identical.
inline bool IsCrackleCountSpell(const CardParams& p)
{
    return p.discount_targets_scale_x && p.x_damage_multiplier > 0;
}

// Crackle target sentinels (distinct from a >= 0 battlefield index): the shared face sentinels.
//   -1  = SELF   (the controller's own face)
//   -2  = OPPONENT face (the win-relevant target; order[0] of CrackleTargetOrder)
constexpr int CRACKLE_SELF_FACE = TARGET_SELF_FACE;
constexpr int CRACKLE_OPP_FACE  = TARGET_OPP_FACE;

// Crackle with Power: ordered EXTRA beneficial targets beyond the opponent face, for the Hinata
// per-target discount + faithful 5X damage. Encoding: >= 0 is a battlefield index (a creature);
// -1 is SELF (the controller's face), offered ONLY when `per_target_dmg < your life` so the self
// hit is non-lethal (the "target yourself for the discount when safe" line). Order is expendable
// first -- opponent creatures (board order), then your NON-Hinata creatures, then self-if-safe,
// then Hinata LAST (targeting her removes the discount for later spells, so she is only taken at
// the maximum count). The searched/declared crackle_targets count picks the first N of this list.
inline std::vector<int> CrackleExtraTargetOrder(const GameState& state, int controller,
                                                int per_target_dmg)
{
    std::vector<int> out;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)   // opponent creatures
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != controller && p.card.IsCreature()) { out.push_back(i); }
    }
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)   // own non-Hinata creatures
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index == controller && p.card.IsCreature() && !IsHinataPermanent(p)) { out.push_back(i); }
    }
    if (per_target_dmg < state.players[controller].life) { out.push_back(-1); }   // self, only if non-lethal
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)   // Hinata LAST
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index == controller && p.card.IsCreature() && IsHinataPermanent(p)) { out.push_back(i); }
    }
    return out;
}

// Number of extra Crackle targets available (cap on the declared count) at this per-target damage.
inline int CrackleExtraTargetCount(const GameState& state, int controller, int per_target_dmg)
{
    return static_cast<int>(CrackleExtraTargetOrder(state, controller, per_target_dmg).size());
}

// Deal Crackle's `per_target_dmg` (= 5X) to the first `count` extra targets from
// CrackleExtraTargetOrder (creatures + self), then destroy lethal creatures (SBA), so a killed
// creature leaves the target pool for later spells. The opponent FACE damage is applied separately
// by the caller. Lockstep between ApplyPlanDirect (rollout/search) and EffectHandler (executor).
inline void CrackleHitExtraTargets(GameState& state, int controller, int per_target_dmg, int count)
{
    if (count <= 0 || per_target_dmg <= 0) { return; }
    std::vector<int> order = CrackleExtraTargetOrder(state, controller, per_target_dmg);
    const int n = std::min(count, static_cast<int>(order.size()));
    std::vector<int> dead;
    for (int s = 0; s < n; ++s)
    {
        int bi = order[s];
        if (bi < 0) { state.players[controller].life -= per_target_dmg; continue; }   // self
        Permanent& cre = state.battlefield[bi];
        cre.damage += per_target_dmg;
        if (cre.damage >= cre.EffectiveToughness()) { dead.push_back(bi); }
    }
    std::sort(dead.begin(), dead.end(), std::greater<int>());   // descending -> erase stays valid
    for (int bi : dead)
    {
        Permanent& cre = state.battlefield[bi];
        state.players[cre.owner_index].graveyard.push_back(cre.card);
        state.battlefield.erase(state.battlefield.begin() + bi);
    }
}

// Crackle with Power FULL target order, opponent face FIRST (CRACKLE_OPP_FACE), then the extra
// beneficial targets (CrackleExtraTargetOrder: opp creatures, own non-Hinata, self-if-safe, Hinata
// last). This is the generic "up to X targets" order that makes the opponent face a NORMAL,
// optional target instead of a forced hit -- the autonomous default picks the first T (face
// included, byte-identical to the old face+extras resolution), while human play may choose any
// subset. Total legal target count = 1 (opp face) + CrackleExtraTargetCount.
inline std::vector<int> CrackleTargetOrder(const GameState& state, int controller,
                                           int per_target_dmg)
{
    std::vector<int> out;
    out.push_back(CRACKLE_OPP_FACE);                                        // opponent face FIRST
    for (int t : CrackleExtraTargetOrder(state, controller, per_target_dmg)) { out.push_back(t); }
    return out;
}

// Deal Crackle's `per_target_dmg` (= 5X) to an EXPLICIT ordered target list (sentinels per
// CrackleTargetOrder: -2 opp face, -1 self face, >= 0 creature), then destroy lethal creatures
// (SBA). Returns true iff the opponent face was among the targets (so the caller can set
// opponent_lost_life_this_turn only when it actually did). Applying [CRACKLE_OPP_FACE] + the first
// N extras is byte-identical to the old "face -= dmg; CrackleHitExtraTargets(N)" path (same order,
// same SBA), so autonomous resolution is unchanged; human play passes an arbitrary chosen subset.
inline bool CrackleApplyTargets(GameState& state, int controller, int per_target_dmg,
                                const std::vector<int>& targets)
{
    bool hit_opp = false;
    if (per_target_dmg <= 0) { return hit_opp; }
    std::vector<int> dead;
    for (int t : targets)
    {
        if (t == CRACKLE_OPP_FACE) { state.players[1 - controller].life -= per_target_dmg; hit_opp = true; }
        else if (t == CRACKLE_SELF_FACE) { state.players[controller].life -= per_target_dmg; }
        else if (t >= 0 && t < static_cast<int>(state.battlefield.size()))
        {
            Permanent& cre = state.battlefield[t];
            cre.damage += per_target_dmg;
            if (cre.damage >= cre.EffectiveToughness()) { dead.push_back(t); }
        }
    }
    std::sort(dead.begin(), dead.end(), std::greater<int>());   // descending -> erase stays valid
    for (int bi : dead)
    {
        Permanent& cre = state.battlefield[bi];
        state.players[cre.owner_index].graveyard.push_back(cre.card);
        state.battlefield.erase(state.battlefield.begin() + bi);
    }
    return hit_opp;
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

// "For each color among permanents you control" (Faeburrow Elder / Bloom Tender): the union of
// card COLORS over the controller's battlefield permanents, in WUBRG order. Thread_local buffer
// with the same safety contract as ReflectedColors (consume within one source's loop; do not hold
// across another Produces* call). Colorless permanents contribute nothing (CR 105.2c).
inline const std::vector<Color>& DomainColors(const GameState& state, int controller)
{
    static thread_local std::vector<Color> cols;
    cols.clear();
    bool have[5] = {false, false, false, false, false};
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller) { continue; }
        for (int ci = 0; ci < 5; ++ci)
        {
            if (p.card.HasColor(static_cast<Color>(ci))) { have[ci] = true; }
        }
    }
    for (int ci = 0; ci < 5; ++ci) { if (have[ci]) { cols.push_back(static_cast<Color>(ci)); } }
    return cols;
}

// ---- One-shot "lump" sac sources as PAYMENT sources (§2a) -------------------------------------
// MTG_TREASURE_PAY_SOURCE -- DEFAULT ON, adopted 2026-08-26 (=0 disables). Routes a 1-mana sac source
// (Treasure Token) through the mana PAYMENT solver instead of the plan enumerator's per-colour
// SacForMana fan, deleting its odometer group outright.
//
// WHY. The colour of a mana source is a payment question, not a plan question -- which is how every
// land is already treated. The enumerated fan makes each Treasure a 3-option odometer group, so a
// 9-Treasure Mirrorwing board enumerates 3^9 = 19,683 states for what is one payment decision.
// Measured counterfactual (MTG_SAC_DUP_CAP=1, 5,120 games, which UNDER-states this since it leaves
// a 3-option group where this leaves none): 1.08x deck-wide but BIMODAL -- 11.78x / 10.66x / 8.02x
// on individual games, and the 2nd most expensive game in the sweep collapses 8x. See
// docs/design/lump-mana-sources-as-payment-sources.md and mirrorwing-search-cost.md.
//
// SCOPED TO amount == 1. Lotus / Lotus Bloom (amount 3) are "N mana all of ONE colour", a payment
// shape the flow matcher does not have (§2a-bis shape A/B); they keep the action-level colour fold.
// A 1-mana source is exactly the existing `add(mask, 1)` shape, so this needs no new solver
// machinery -- which is the whole reason Treasures come first.
inline bool TreasurePaySourceEnabled()
{
    static const bool v = EnvOn("MTG_TREASURE_PAY_SOURCE", true);
    return v;
}

// A sac source the PAYMENT solver owns. `produces.empty()` keeps this to the sources that carry no
// colour of their own today (the whole reason they are invisible to the payment path: pay_produces()
// comes back empty, `makes` is false, and every source scan skips them).
inline bool IsPaySacSource(const CardDefinition& def)
{
    return TreasurePaySourceEnabled()
        && def.params.sac_for_mana_amount == 1
        && def.params.produces.empty();
}

// Cracking a Treasure SACRIFICES it; the payment path can only tap. Erasing mid-payment is unsafe
// (the source loops hold `Permanent&` and derive the reserved-mask index from
// `&p - battlefield.data()`; ApplySacForMana carries the same warning), so the tap marks it TAPPED
// and the erase is deferred to each payment success path via this helper.
//
// No new Permanent field is needed: nothing else in the engine taps a Treasure, so "tapped pay-sac
// source" means exactly "cracked during this payment", and a FAILED payment restores the untapped
// state from its `bf_pre` snapshot before this is ever reached. Inert (and byte-identical) when the
// flag is off, because IsPaySacSource is then always false.
inline void CommitPaySacSacrifices(GameState& state, int controller)
{
    if (!TreasurePaySourceEnabled()) { return; }
    for (int i = static_cast<int>(state.battlefield.size()) - 1; i >= 0; --i)
    {
        const Permanent& p = state.battlefield[static_cast<std::size_t>(i)];
        if (p.controller_index != controller || !p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && IsPaySacSource(*d))
        { state.battlefield.erase(state.battlefield.begin() + i); }
    }
}

// §2a FRESH-HOLD (USER doctrine 2026-08-12, strict-bar fix 2026-08-25): a magnetless Gold Rush is
// "never a this-turn mana play", so a pay-sac source that ENTERED THIS TURN is a BANK -- invisible
// to payment and to every pool/colour scan -- unless a copy-magnet (Zada / Mirrorwing) is live,
// where fan-minted Treasures legitimately fund same-turn continuation. Banked (pre-turn) Treasures
// always pay. Why: §2a made same-PLAN consumption enumerable (the SacForMana fan could not emit an
// action for a not-yet-existing Treasure), which turned clause-(c) Gold Rush casts into net-minus-
// one-mana same-turn ramp lines the search then preferred (mw22/mw68 traces; unbounded-persistent,
// value-leaf-independent). MTG_PAYSAC_FRESH_HOLD=0 restores the unheld §2a behaviour for A/B;
// inert when MTG_TREASURE_PAY_SOURCE is off (IsPaySacSource is then always false).
inline bool PaySacFreshHoldEnabled()
{
    static const bool v = EnvOn("MTG_PAYSAC_FRESH_HOLD", true);
    return v;
}

inline bool CopyMagnetLive(const GameState& state, int controller)
{
    for (const Permanent& q : state.battlefield)
    {
        if (q.controller_index != controller) { continue; }
        const CardDefinition* qd = CardDatabase::Instance().LookupCached(q.card);
        if (qd && qd->params.copies_solo_targeted_spells) { return true; }
    }
    return false;
}

// ---- The "GOLD RUSH POSITIVE" rule (USER, 2026-08-27) -----------------------------------------
//
// USER: "use the 'Gold Rush positive' rule to decide whether we need to consider casting it
// earlier. If it doesn't add mana or fix colours then we hold it. This is true for any point prior
// to 15."
//
// A treasure-minting trick is a MANA SOURCE only when the Treasures it mints are worth at least
// what casting it costs. Gold Rush is {1}{G} for ONE Treasure -- pay 2, get 1, i.e. MANA-NEGATIVE
// bare. It becomes an engine only once a copy magnet is live, because a solo-target trick aimed at
// the magnet is copied once per OTHER creature (ResolveSoloTargetTrick's `order`: every other own
// creature/animated permanent, then the original) and EVERY copy mints its own Treasure. So the
// resolution count is exactly "own creatures I control" with a magnet out, and 1 without.
//
// NET >= 0 is the bar, and it covers both halves of the USER's sentence: net > 0 ADDS mana (magnet
// + >=2 other bodies), and net == 0 still FIXES COLOURS -- two Treasures for {1}{G} converts a
// green pip into two WILD at no loss, which is exactly the affordability change a colour-screwed
// line needs. Net < 0 is the bare pump spell, and it holds.
//
// OPTIMISTIC on the target choice, deliberately. Gold Rush is trick_up_to_one, so an UNTARGETED
// cast is also offered (it mints only the base Treasure and triggers no magnet), and which mode a
// given cast used is not recoverable from the board afterwards. Both consumers of this predicate
// are PRUNES -- an early cast-order rung, and a condemnation exemption -- so over-estimating the
// net only ever offers MORE lines, the safe direction under the no-lossy-truncation bar.
inline int TreasureSpellNetMana(const GameState& state, int controller, const CardDefinition& def)
{
    if (def.params.creates_treasures <= 0) { return 0; }
    int resolutions = 1;
    if (def.params.solo_target_trick && CopyMagnetLive(state, controller))
    {
        int bodies = 0;
        for (const Permanent& q : state.battlefield)
        {
            if (q.controller_index != controller) { continue; }
            if (q.card.IsCreature() || q.is_animated) { ++bodies; }
        }
        resolutions = std::max(1, bodies);   // magnet + each OTHER body == every body
    }
    return def.params.creates_treasures * resolutions - def.card.m_mana_cost.ManaValue();
}

// FRESH-SPEND AXIS (MTG_FRESH_SPEND_AXIS, default ON, adopted 2026-08-26; =0 disables): make the fresh-hold doctrine above a
// searched BRANCH for exactly one shape -- a plan that converts the fresh mint into a THIS-TURN
// kill (mw136: T5 Gold Rush -> crack the mint -> Luxurious Libation X+1 = exact lethal; banking
// has zero option value when the game ends now). The doctrine stays the DEFAULT world: the train
// measured the global release strictly worse (73 slower / 31 faster, mwA/mwB 2026-08-26), because
// spend-vs-bank is a speculative next-turn comparison the search misprices -- so spend-variants
// are ADMISSIBLE ONLY when their simulated combat is lethal the turn they apply (FSLineWin
// discards every other freshmode variant before its tail is ever scored).
inline bool FreshSpendAxisEnabled()
{
    static const bool v = EnvOn("MTG_FRESH_SPEND_AXIS", true);
    return v;
}

// Plan-scoped release of the fresh-hold (Plan::freshmode_choice, ScriptedFreshMode below):
// 1 == this plan variant is the SEARCHED "spend the fresh mint" branch. Like the tapmode pin it
// is a whole-plan STATIC pin (never consumed), installed by both apply paths so search and
// executor pay in lockstep.
extern thread_local int g_scripted_freshmode;

// ONE shared reader for "is the fresh-hold biting right now" -- every fresh-hold consumer
// (PaySacSpendableNow, MintedTreasureSpendable, the two enumeration mint-credit twins) must gate
// on THIS, not on PaySacFreshHoldEnabled() directly, so the freshmode pin releases all of them
// together (a site left on the raw flag would price a world the payer then refuses).
inline bool FreshHoldActive()
{
    return PaySacFreshHoldEnabled() && g_scripted_freshmode != 1;
}

// CURRENT PAYMENT's coloured need (W,U,B,R,G), published by TapForCostSharedOnce for the
// duration of one payment and zero/dead everywhere else. Consumer: the sole-colour-provider
// rank tier (MTG_SCARCE_COLOR_HOLD, DecisionProviders.cpp) -- the stranding it prevents exists
// ONLY when the payment being ordered does NOT itself need the scarce colour (mw326: Gold Rush
// {1}{G} eating the lone {R} source), so a payment that needs the colour keeps today's rank.
// The first, unconditioned tier measured +0.067 summed on held-out mirrorwing (12/12 cells
// worse) by reordering every mint-plan payment; this gate trims it to the hazard shape.
// live=false => tier inactive (conservative: prepay combined solves and every non-payment rank
// read are untouched). Same publish-a-scalar shape as the (reverted) fix-1 g_pay_remaining_pips.
inline thread_local int  g_pay_colored_need[5] = {0, 0, 0, 0, 0};
inline thread_local bool g_pay_need_live       = false;
struct PayNeedScope
{
    int  prev[5];
    bool prev_live;
    PayNeedScope(int w, int u, int b, int r, int g)
    {
        for (int i = 0; i < 5; ++i) { prev[i] = g_pay_colored_need[i]; }
        prev_live = g_pay_need_live;
        g_pay_colored_need[0] = w; g_pay_colored_need[1] = u; g_pay_colored_need[2] = b;
        g_pay_colored_need[3] = r; g_pay_colored_need[4] = g;
        g_pay_need_live = true;
    }
    ~PayNeedScope()
    {
        for (int i = 0; i < 5; ++i) { g_pay_colored_need[i] = prev[i]; }
        g_pay_need_live = prev_live;
    }
    PayNeedScope(const PayNeedScope&)            = delete;
    PayNeedScope& operator=(const PayNeedScope&) = delete;
};

// SAC-FODDER PAYS FIRST (MTG_SAC_FODDER_PAYS, default ON, adopted 2026-08-26; =0 disables --
// train: every stompy cell equal-or-faster; held-out overnight: net faster, the one slower
// searched game gi739 recovers at unbounded budget; solo-vs-batch controls byte-identical, so
// the deltas are pure lever effect): while paying
// for a cast that sacrifices a specific creature as an ADDITIONAL COST (Natural Order; the
// searched victim rides own_targets as a card m_number), that victim's mana ability is FREE --
// the body is already spent by the cost itself, so tapping it can never lose an attacker or a
// later cast. st993's T4 measured the miss: NO {2}{G}{G} on 3 Forests + {Arbor Elf(victim),
// Priest, Archdruid} tapped ARCHDRUID for the 4th mana (attack-rung F: "one big body pays what
// N flat bodies would" -- correct when the bodies are interchangeable, wrong when one body is
// leaving anyway), removing a 5-power-under-Hoof attacker: alpha 13 < 16, win 5; tapping the
// doomed Arbor instead = alpha 18, win 4. Published RAII-scoped around each cast's payment at
// all three pay sites (rollout apply cast, executor CastSpellFromHand, BatchPrepayMainCasts) --
// executor/rollout lockstep. Consumer: TapForCostSharedOnce's rank selection (ManaPayment.cpp),
// which promotes the exact victim COPY (m_number match) ahead of every other source. 0 = dead.
inline bool SacFodderPaysEnabled()
{
    static const bool v = EnvOn("MTG_SAC_FODDER_PAYS", true);
    return v;
}
inline thread_local int g_pay_sac_victim = 0;
struct PaySacVictimScope
{
    int prev;
    explicit PaySacVictimScope(int victim_num) : prev(g_pay_sac_victim)
    { if (SacFodderPaysEnabled()) { g_pay_sac_victim = victim_num; } }
    ~PaySacVictimScope() { g_pay_sac_victim = prev; }
    PaySacVictimScope(const PaySacVictimScope&)            = delete;
    PaySacVictimScope& operator=(const PaySacVictimScope&) = delete;
};

// The state-aware twin of IsPaySacSource: use at every "can this permanent pay / produce NOW"
// site (payment usability + the pool/colour scans that must promise exactly what payment
// delivers). Def-only IsPaySacSource remains correct at the identity sites (odometer exclusion,
// rank, the tapped-means-cracked erase above).
inline bool PaySacSpendableNow(const GameState& state, const Permanent& p, const CardDefinition& def)
{
    if (!IsPaySacSource(def)) { return false; }
    if (!FreshHoldActive() || !p.entered_this_turn) { return true; }
    return CopyMagnetLive(state, p.controller_index);
}

// The colours a mana source currently produces. Identical to def.params.produces (by const ref,
// zero cost, byte-identical) for every normal source; the dynamic Reflecting-Pool union only for
// a `reflecting` source; the dynamic colour-domain union for a `domain_mana` source (Faeburrow /
// Bloom Tender). controller = the source's controller (active player on the battlefield).
// The colours an energy-gated source still makes with NO energy left: its colourless modes only.
// Aether Hub's "{T}: Add {C}" is a separate, free ability, so a spent-out Hub degrades to a plain
// {C} source rather than producing nothing -- and its {C} is never energy-gated, which matters
// because that is what pays Eldrazi Displacer's {2}{C} pip.
//
// thread_local, consumed immediately by the caller, exactly like ReflectedColors' buffer.
inline const std::vector<Color>& EnergySpentColors(const CardDefinition& def)
{
    static thread_local std::vector<Color> buf;
    buf.clear();
    for (Color c : def.params.produces)
    { if (c == Color::Colorless) { buf.push_back(c); } }
    return buf;
}

inline const std::vector<Color>& EffectiveProduces(const GameState& state, int controller,
                                                   const CardDefinition& def, bool in_hand = false)
{
    if (def.params.domain_mana) { return DomainColors(state, controller); }
    // A pay-sac source makes one mana of ANY colour. Giving it a real `produces` is what admits it to
    // every source scan, and AddSourceToPool already credits a multi-colour source as `wild += amt`
    // -- exactly the credit the SacForMana action's `ritual_float` gives today, so this is a SWAP of
    // the accounting, not an addition to it (the action is removed in the same change).
    if (IsPaySacSource(def))
    {
        // COLOUR-PINNED sac source (Eldrazi Spawn: "Sacrifice this creature: Add {C}"). One real
        // colour, so AddSourceToPool credits it as that colour rather than wild -- {C} pays generic
        // pips and nothing else. Without this pin the Spawn would read as a rainbow source and
        // could pay a {R} or {G} pip it cannot actually pay.
        if (!def.params.sac_for_mana_color.empty())
        {
            static const std::vector<Color> kColorless{ Color::Colorless };
            static const std::vector<Color> kW{ Color::White }, kU{ Color::Blue },
                                            kB{ Color::Black }, kR{ Color::Red },
                                            kG{ Color::Green };
            switch (def.params.sac_for_mana_color[0])
            {
                case 'C': return kColorless;
                case 'W': return kW;
                case 'U': return kU;
                case 'B': return kB;
                case 'R': return kR;
                case 'G': return kG;
                default:  break;
            }
        }
        static const std::vector<Color> kAnyColor{ Color::White, Color::Blue, Color::Black,
                                                   Color::Red,   Color::Green };
        return kAnyColor;
    }
    // ENERGY-GATED COLOURS (Aether Hub). The coloured half of "{T}, Pay {E}: Add one mana of any
    // color" is available only while the controller can pay, so strip to the colourless modes when
    // they cannot. IN HAND the card is not stripped: a Hub in hand brings its own energy with it
    // (etb_energy), so the mulligan/fixing heuristics should see it as the real fixer it is.
    if (def.params.energy_per_colored_tap > 0 && !in_hand
        && state.players[controller].energy_counters < def.params.energy_per_colored_tap)
    {
        return EnergySpentColors(def);
    }
    if (!def.params.reflecting) { return def.params.produces; }
    return ReflectedColors(state, controller, in_hand);
}

// D12 payment context: the current TapForCost* call is paying the activation cost of an ability
// whose SOURCE is a battlefield creature of the chosen type (Burning-Fist / Sethron pumps; the
// engine's "chosen type" is simplified to "any creature", exact for the mono-tribal decks that
// run these lands). Secluded Courtyard's oracle text permits its restricted coloured mana for
// exactly this case, where Cavern of Souls / Unclaimed Territory / Sliver Hive permit casts only
// -- so this cannot ride the for_creature bool (that would over-permit the cast-only lands) and
// is a per-card param (colored_creature_ability_ok) plus this scoped context flag. thread_local
// RAII (the g_search_candidate_enum pattern) rather than threading a third bool through every
// TapForCost signature. False everywhere except inside an ActivatePump payment -> byte-identical
// for every deck without a colored_creature_ability_ok land.
inline bool& PayingCreatureAbility()
{
    static thread_local bool v = false;
    return v;
}
struct CreatureAbilityPayScope
{
    bool prev;
    CreatureAbilityPayScope()  : prev(PayingCreatureAbility()) { PayingCreatureAbility() = true; }
    ~CreatureAbilityPayScope() { PayingCreatureAbility() = prev; }
};

// Colours a source may produce to pay for THIS spell (payment context -> for_creature is known).
// Identical to EffectiveProduces for every source EXCEPT a colored_creature_only source (Unclaimed
// Territory / Cavern of Souls: {C} free, coloured only for a creature spell of the chosen type,
// simplified to any creature): when the spell is NOT a creature it yields only {Colorless}, so its
// coloured mana can't pay a coloured/{C}-typed... no: only its {C} survives, which pays generic pips
// but not coloured pips. for_creature == true (or the flag off) -> the full EffectiveProduces list.
// Returns a thread_local buffer when it strips (safe like ReflectedColors: consumed within one
// source's loop, never held across another Produces* call). Byte-identical for every deck without a
// colored_creature_only source.
inline const std::vector<Color>& ProducesForPayment(const GameState& state, int controller,
                                                    const CardDefinition& def, bool for_creature)
{
    const std::vector<Color>& base = EffectiveProduces(state, controller, def);
    if (!def.params.colored_creature_only || for_creature) { return base; }
    // D12: Secluded Courtyard's clause also covers activated abilities of creature sources.
    if (def.params.colored_creature_ability_ok && PayingCreatureAbility()) { return base; }
    static thread_local std::vector<Color> only_c;
    only_c.clear();
    for (Color c : base) { if (c == Color::Colorless) { only_c.push_back(c); } }
    return only_c;
}

// ---- colored_creature_only legality audit (MTG_CCO_AUDIT; MEASUREMENT ONLY) ------------------
// "Spend this mana only to cast a creature spell of the chosen type" (Unclaimed Territory / Cavern
// of Souls / Sliver Hive / Secluded Courtyard). Called at EVERY site that taps a source for a
// specific colour -- the rollout's and the executor's tap_source, and the shared backtracker -- so a
// violation cannot hide in one path. A tap of such a source for a NON-colorless colour while paying
// a NON-creature spell is illegal; the counter must read violations=0. Every affected deck's
// creatures share ONE creature type (Dragonstorm all Dragon, Knights all Knight, slivers all
// Sliver), so the engine's "any creature" simplification is exact for them and this check is
// complete. Off -> two predictable branches, no output. See docs/design/post-breakpoint-search.md.
inline std::atomic<long long>& CcoAuditTaps()
{ static std::atomic<long long> n{0}; return n; }
inline std::atomic<long long>& CcoAuditViolations()
{ static std::atomic<long long> n{0}; return n; }
inline bool CcoAuditOn()
{ static const bool on = EnvOn("MTG_CCO_AUDIT"); return on; }

inline void CcoAuditTap(const CardDefinition& def, Color col, bool for_creature)
{
    if (!CcoAuditOn() || !def.params.colored_creature_only) { return; }
    CcoAuditTaps().fetch_add(1, std::memory_order_relaxed);
    if (col != Color::Colorless && !for_creature
        // D12: a coloured tap paying a creature-source ability is LEGAL for Secluded Courtyard.
        && !(def.params.colored_creature_ability_ok && PayingCreatureAbility()))
    {
        CcoAuditViolations().fetch_add(1, std::memory_order_relaxed);
    }
}

// Dumped once at process exit (a static object's destructor), like the affordability audit.
struct CcoAuditDump
{
    ~CcoAuditDump()
    {
        if (!CcoAuditOn()) { return; }
        std::fprintf(stderr, "[cco-audit] colored_creature_only taps=%lld  ILLEGAL(coloured for a "
                             "non-creature spell)=%lld\n",
                     CcoAuditTaps().load(), CcoAuditViolations().load());
    }
};
inline CcoAuditDump& CcoAuditDumper() { static CcoAuditDump d; return d; }

// ---- Breakpoint payment trace (MTG_BP_TRACE; DIAGNOSIS ONLY, no behaviour) -------------------
// Print the mana situation immediately BEFORE a cast pays, from BOTH the rollout (ApplyPlanDirect's
// apply_one) and the real executor (CastSpellFromHand), so the two can be diffed cast-for-cast.
// The searched breakpoint continuation is a plan the ROLLOUT proved payable; if the executor's
// payment strands a source the rollout spent, the realised turn stops mid-script and the game comes
// in worse than the predicted line with no search defect to find. See post-breakpoint-search.md.
inline void BpTraceCast(const char* side, const GameState& state, const std::string& name,
                        const ManaCost& cost, bool for_creature)
{
    const ManaPool& f = state.floating_mana;
    std::string untapped;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d == nullptr || !d->card.IsLand()) { continue; }
        if (!untapped.empty()) { untapped += ","; }
        untapped += p.card.m_name.str();
    }
    std::string dorks;   // untapped, TAP-ELIGIBLE mana dorks (sickness/haste-aware)
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != state.active_player_index || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d == nullptr || d->tmpl != CardTemplate::ManaDork) { continue; }
        if (!CanTapNow(p, state.battlefield)) { continue; }
        if (!dorks.empty()) { dorks += ","; }
        dorks += p.card.m_name.str();
        dorks += "#" + std::to_string(p.card.m_number);
    }
    std::fprintf(stderr,
                 "[bp-pay] %-5s T%-2d cast=%-24s cost=%d/W%d U%d B%d R%d G%d C%d creature=%d"
                 " float=W%d U%d B%d R%d G%d C%d wild%d untapped=[%s] dorks=[%s]\n",
                 side, state.turn_number, name.c_str(),
                 cost.generic, cost.white, cost.blue, cost.black, cost.red,
                 cost.green, cost.colorless, for_creature ? 1 : 0,
                 f.white, f.blue, f.black, f.red, f.green, f.colorless, f.wild,
                 untapped.c_str(), dorks.c_str());
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
                   || (def->tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield))
                   || def->params.mana_rock;
        if (!is_src) { continue; }
        if (!GraveyardFuelLive(state, active, *def)) { continue; }   // Deathrite: no gy land
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
    static const bool v = !EnvOn("MTG_TAP_LEGACY");
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
    static const bool v = EnvOn("MTG_RESERVE");
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
    static const bool v = !EnvOn("MTG_NO_DEPLETION_RESERVE");
    return v;
}

// Depletion tap ORDER (user doctrine 2026-08-27, treasure_hunt seed 8): a depletion land's tap
// SPENDS a counter (finite -- the last one sacrifices the land), so (1) it taps AFTER a plain land
// of the same flexibility tier (ManaSourceRankBase +1: mono 10->11, dual 20->21, same shape as the
// drip-land nudge) and (2) among equal-rank depletion lands the one with MORE counters taps first
// (TapForCostSharedOnce tiebreak): tapping the 2-counter copy leaves two lands = two taps available
// next turn, while tapping the 1-counter copy kills it for the same total mana -- the rule
// preserves per-turn burst, not resources. Motivating board: Fiery Islet's sac-to-draw {1} tapped a
// last-counter Saprazzan Skerry (2 produced, land dies, {U} wasted) over a fresh Island on a
// battlefield-order rank tie, which also stranded the [Island, sac Islet, retrace Throes] line the
// user tried to play. Default ON; off-switch MTG_NO_DEPLETION_TAP_ORDER for A/B.
inline bool DepletionTapOrderEnabled()
{
    static const bool v = !EnvOn("MTG_NO_DEPLETION_TAP_ORDER");
    return v;
}

// Filter-{C}-first (USER 2026-08-27, treasure_hunt s11 T6): a FILTER land tapping its plain "{T}:
// Add {C}" mode to pay a GENERIC pip is the least flexible mana on the board -- the same doctrine
// as the colourless-before-mono tier (rank 5) -- yet the scarcity loop ranked the filter 25 (past
// the duals), so cycling Remote Isle tapped the REAL dual (Thundering Falls) and left Cascade
// Bluffs "up on its own": a feeder-less filter that can only ever make {C}. Rank the filter's {C}
// mode at 6 for generic/{C} pips: after a true {C}-only land (5 -- spending that first keeps the
// filter's conversion option alive), before every coloured tier. The coloured filter MODE (kind 2)
// keeps its normal rank; whole-turn "save the filter for a conversion this line needs" remains the
// prepay's job, not this per-pip order. Default ON; MTG_NO_FILTER_C_FIRST reverts for A/B.
inline bool FilterCFirstEnabled()
{
    static const bool v = !EnvOn("MTG_NO_FILTER_C_FIRST");
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
    static const bool v = !EnvOn("MTG_NO_ATTACKER_RESERVE");
    return v;
}

// Whole-turn MANA-CREATURE reservation (BatchPrepayMainCasts): hold back EVERY untapped mana
// creature -- not just the greatest-power attacker AttackerReserveEnabled already spares -- when the
// turn's COMBINED main-cast cost is payable off the lands alone. This is the user's stated rule from
// the play sessions, recorded in docs/design/mana-source-reservation.md: "if the current line can be
// paid while leaving all special sources untapped, just leave them all up -- branch only when they
// COMPETE." A land has no use but its mana; a creature does (it attacks, it carries Exalted, it is a
// legal target for a pump/copy trick, it can be sacrificed), so spending a dork on a pip a land could
// have covered throws that away for nothing. Reported twice from the viewer (Mirrorwing s1 T4: the
// Hierarch tapped with a Mountain untapped; s24 T4: the Hierarch tapped instead of a Sandstone
// Needle, which cost the attack that would have won the game).
//
// Same all-or-nothing "leave out if you can" soundness as depletion/attacker: it only declines to
// PRE-tap, never removes a source, so a later post-draw re-solve can still tap what it needs; and if
// holding them makes the turn unaffordable (or forces an ambiguous wild tap) the unrestricted solve
// runs instead, so no line is ever lost. Default ON; off-switch MTG_NO_DORK_RESERVE for A/B.
inline bool DorkReserveEnabled()
{
    static const bool v = !EnvOn("MTG_NO_DORK_RESERVE");
    return v;
}

// ---- Plan-traits override levers (docs/design/mana-order-and-reserve-overhaul.md, layer 3). ----
// All DEFAULT OFF (=1 enables) while the bundle is measured; adoption flips each winner to
// default-ON with a MTG_NO_* hatch and this comment records the outcome. Every consumer treats a
// null CurrentPlanTraits() as "lever off" -- outside a plan apply nothing changes regardless.

// MAIN-2 RELEASE (MTG_M2_RELEASE): in the POST-combat main the "hold the bodies" reservation buys
// nothing in a goldfish -- combat already happened and there is no opponent turn to block in -- so
// the prepay's creature holds (dork + attacker classes) are skipped there and the bodies pay
// freely. Depletion/one-shot holds are phase-blind (a wasted counter is wasted in any phase).
// Revisit for 1v1, where an untapped blocker in main 2 is real value.
inline bool M2ReleaseEnabled()
{
    static const bool v = EnvOn("MTG_M2_RELEASE", true);
    return v;
}

// PUMP-TARGET HOLD (MTG_PUMP_TARGET_HOLD): when the plan casts an own-creature pump/trick, the
// creature it will target is the body worth holding HARDEST -- the reserve ladder gains a
// provider-narrowed rung (DecisionProvider::ReserveCreatureHold) between "hold every dork" and
// "hold nothing", and the partial-release order keeps the projected target until last. Replaces
// the every-turn greatest-power-attacker bet (AttackerReserveEnabled's documented hack) with a
// plan-gated exact hold. USER 2026-08-25: "with targeted pumps I would have the default only
// reserve the pumped creature" -- and the Mirrorwing copy-magnet override holds the whole board.
inline bool PumpTargetHoldEnabled()
{
    static const bool v = EnvOn("MTG_PUMP_TARGET_HOLD", true);
    return v;
}

// MTG_PUMP_TARGET_HOLD's CORE-side half: the card.m_number of the body the backtracker should
// reach for LAST within its spare-the-creatures dork block (the projected pump target). Core
// cannot see ai/PlanContext, so the apply sites mirror the trait into this thread_local alongside
// PlanTraitsScope -- the g_flow_src_mask pattern. 0 = no hint = the shipped candidate order.
inline thread_local int g_tap_keep_last_card = 0;
struct TapKeepLastScope
{
    int prev;
    explicit TapKeepLastScope(int v) : prev(g_tap_keep_last_card) { g_tap_keep_last_card = v; }
    ~TapKeepLastScope() { g_tap_keep_last_card = prev; }
    TapKeepLastScope(const TapKeepLastScope&)            = delete;
    TapKeepLastScope& operator=(const TapKeepLastScope&) = delete;
};

// ONE-SHOT RESERVE (MTG_ONESHOT_RESERVE) -- §2b "waste is the trigger" (lump doc): a pay-sac
// one-shot (§2a Treasure) is GONE when spent, so hold it whenever the turn pays without it -- at
// the whole-turn ladder (its own class, below creatures and above depletion) AND at the
// per-payment reserve-first attempt (single-cast turns decline the prepay, and rank 26 would
// otherwise spend the Treasure eagerly on a turn with slack). When the payment genuinely needs
// it, the fallback releases it and rank 26 spends it ahead of the flexible sources (exactness).
// The provider can flip the bias per turn (SpendOneShotsFreely: Mirrorwing spends Treasures on
// go-off turns to keep bodies untapped). Inert unless MTG_TREASURE_PAY_SOURCE is on (nothing else
// is a pay-sac source). KNOWN RISK CLASS to probe in the A/B: a per-payment hold once regressed by
// stranding a later same-turn cast (treasure_hunt s3044, the retired MTG_RESERVE scheme) -- the
// whole-turn ladder covers multi-cast turns jointly, but watch the declined-prepay shapes.
inline bool OneShotReserveEnabled()
{
    static const bool v = EnvOn("MTG_ONESHOT_RESERVE", true);
    return v;
}

// SCALER PLAN BIAS (MTG_SCALER_PLAN_BIAS): a subtype scaler's tap order depends on WHAT THE PLAN
// DOES (USER 2026-08-24). Casting its food this turn (more Elves for Priest of Titania) -> tap it
// LAST so the burst counts them. Attack turn with no food -> tap it FIRST among creatures: one
// big body pays what N small ones would, keeping the flat dorks untapped to swing. Resolves the
// static tension mana-creature-tap-order.md §5b measured (hold-back helps some decks, costs
// stompy) plan-conditionally. Only meaningful with the creature band (MTG_DORK_TAP_LAST) on --
// without the band, creatures rank by colour and "first among creatures" is not expressible.
inline bool ScalerPlanBiasEnabled()
{
    static const bool v = EnvOn("MTG_SCALER_PLAN_BIAS", true);
    return v;
}

// SCARCE-COLOR HOLD (MTG_SCARCE_COLOR_HOLD, default ON, adopted 2026-08-26 (=0 disables); overhaul
// ledger "mw326 DTL"). On a multi-cast plan whose whole-turn prepay DECLINED (the joint combined
// cost is unpayable up front -- e.g. it needs a mid-turn Treasure mint), the per-cast greedy pays
// each cast with no view of the NEXT cast's colour needs. mw326 (Mirrorwing s4330 gi326): under
// MTG_DORK_TAP_LAST's lands-first order, Gold Rush {1}{G} was paid by Gruul Turf -- the board's
// ONLY {R} source, covering both pips at rank 10 ("fixed-multi: no choice") -- so Mirrorwing
// Dragon's {R}{R} stranded and the T4 cast (whose second {R} the mint itself would have covered)
// died, win 5 -> 6. The clean arm dodged it only by index tie-break (the Mystics also rank 10 and
// sit earlier on the battlefield); the hole is order-independent. The fix is the standard lossless
// mask shape (held-first attempt, unrestricted retry -- OneShotHoldMask's contract): while a plan
// apply is paying, hold any untapped source that is a SCARCE provider of a colour the plan's other
// casts still need (providers <= remaining pips of that colour beyond this payment's own cost).
// A payment that genuinely needs the held source gets it back on the retry, so no cast is lost --
// the hold only picks WHICH legal payment is committed. Null traits -> 0 (the MW gi75 rollout
// contract).
inline bool ScarceColorHoldEnabled()
{
    static const bool v = EnvOn("MTG_SCARCE_COLOR_HOLD", true);
    return v;
}

// Should the apply paths compute PlanTraits at all? One check so the builder costs nothing while
// every consumer lever is off (and the scope then installs nullptr = today's behaviour).
inline bool PlanTraitsWanted()
{
    static const bool v = M2ReleaseEnabled() || PumpTargetHoldEnabled()
                       || OneShotReserveEnabled() || ScalerPlanBiasEnabled()
                       || ScarceColorHoldEnabled()   // ScarceColorHoldMask (ManaPayment.cpp)
                       || EnvOn("MTG_M2_PAYLOAD_RESERVE", true);   // PayloadReserveMask (ManaPayment.cpp)
    return v;
}

// ATTACKER-ONLY RUNG on the prepay reservation ladder (MTG_TAP_ATTACKER_RUNG, default ON, adopted 2026-08-26; =0 disables).
// The ladder above is all-or-nothing per class: hold EVERYTHING reservable, and if that is
// unaffordable fall straight to the unrestricted solve. When the turn needs exactly one body's
// mana, "hold every dork" is infeasible and the fallback holds NOTHING -- so the joint solve is
// free to spend the ONE creature whose body the turn needs (the attacker / pump target) and pay
// the same cost that a different, equally legal assignment would have covered off a spare dork.
// This rung inserts the missing middle step: hold JUST the greatest-power attacker (the bit
// AttackerReserveEnabled computes, which DorkReserve currently swallows into the all-dorks mask).
//
// LOSSLESS by the same "leave out if you can" argument as every other rung: it is one extra FIRST
// TRY that declines to pre-tap; if it fails, the unrestricted solve still runs, so no payment and
// no line becomes unreachable (Rule 0b infinite-budget test). Cost is at most one extra
// TapForCostBacktrack per prepay that already failed its full hold.
//
// Origin: Anti-Lifegain gi8 (docs/design/antilife-main-phase-split.md 21l). Board = 3 lands +
// Ignoble Hierarch + Birds of Paradise; the turn's combined m1 cost is 4, so one dork must tap.
// Hold-all fails (3 lands = 3 mana), the plain solve taps the HIERARCH -- the lone attacker and
// the Invigorate target -- and the turn's 5 combat damage (and the game's T3 kill) vanishes, while
// assigning W<-Temple Garden and the generic pip to Birds pays the identical cost with the
// attacker left up. Verified: MTG_NO_DORK_RESERVE=1 (which narrows the mask to exactly this rung's
// attacker bit) recovers the T3 win.
inline bool TapAttackerRungEnabled()
{
    static const bool v = EnvOn("MTG_TAP_ATTACKER_RUNG", true);
    return v;
}

// POWER-AWARE body ordering for the payments the reservation does NOT own (MTG_TAP_POWER_ORDER,
// default off). TapSpareCreaturesEnabled below sorts mana creatures to the BACK of the
// backtracker's candidate list, but WITHIN that group the order is raw battlefield order -- so
// which body burns is decided by which dork happened to enter first, not by what it is worth. A
// 4/4 pumped attacker and an untouched 0/1 Birds are interchangeable to that sort, and the
// first-solution-wins DFS spends whichever sits earlier. Ordering the group by EFFECTIVE POWER
// ASCENDING makes the DFS reach for the CHEAPEST body first, so a pumped/large creature is tapped
// only when the payment genuinely needs it. (This is per-PERMANENT state, which is why it cannot
// live in ManaSourceRank -- that hook takes a CardDefinition and cannot see counters, temp pump,
// or animation. Extending the scarcity path the same way needs that signature widened.)
//
// LOSSLESS: this PERMUTES the DFS candidate list, it never caps it -- the backtracker still
// descends into a big body when nothing else pays (Rule 0b). Same shape and placement constraint
// as the spare-creatures partition it refines (must precede the dup-collapse chain build).
inline bool TapPowerOrderEnabled()
{
    static const bool v = EnvOn("MTG_TAP_POWER_ORDER");
    return v;
}

// The same "a creature is worth more than its mana" rule, one layer down, for the payments the
// reservation above does NOT own. TapForCostBacktrackWorker walks its candidate sources in raw
// BATTLEFIELD ORDER and is first-solution-wins -- it taps cands[0] and recurses, so cands[0] is
// spent whenever ANY payment containing it exists. So on every payment the greedy strands on, and
// on every ladder rung where the reservation has released its hold, WHICH sources burn was decided
// by the order permanents happen to sit in. Sorting mana creatures to the back of that list makes
// the first solution found spend the lands and reach for a body only when the payment needs one.
//
// LOSSLESS: this permutes the DFS candidate list, it does not cap it -- the backtracker still
// descends into a creature when nothing else pays, so no tap set becomes unreachable (the
// heuristic skill's Rule 0b infinite-budget test). Default ON; off-switch MTG_NO_TAP_SPARE_CREATURES.
//
// Measured on three DISJOINT seed sets, net avg win turn summed over every case in the tier
// (negative = better): smoke -0.0303, regression -0.0410, held-out -0.0517. It also cuts backtracker
// nodes 11.4% (deterministic, MTG_TAP_STATS) -- irrelevant next to the backtracker's ~1% share of
// runtime, but it is not a cost. Sorting the SAME list by ManaSourceRank instead measured WORSE
// (+0.0050 smoke) -- scarcity-first is the right rule for the per-cast greedy and the wrong one here,
// and it is the same verdict docs/design/flow-guided-tap-order.md reached independently. The gain
// concentrates on Mirrorwing (-0.048 held-out, an order of magnitude over any other deck) because
// Zada / Mirrorwing Dragon copy a solo-target trick for each OTHER creature you control: an untapped
// creature is a copy target AND an attacker, so a 1/1 Elvish Mystic tapped for {G} forfeits its Gold
// Rush copy and the swing that copy was for. See docs/design/mana-source-reservation.md.
inline bool TapSpareCreaturesEnabled()
{
    static const bool v = !EnvOn("MTG_NO_TAP_SPARE_CREATURES");
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
                   || (def->tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield))
                   || def->params.mana_rock;
        if (!is_src) { continue; }
        if (!GraveyardFuelLive(state, active, *def)) { continue; }   // Deathrite: no gy land
        return true;
    }
    return false;
}

// True if the active player controls an untapped filter / ramp-filter mana source (Cascade Bluffs
// is_filter, Ferrous Lake ramp_filter) whose colour conversion the flat pool cannot model. Used to
// gate the payment's floating-fed-filter retry (see TapForCostSharedOnce, ManaPayment.cpp)
// so that retry is reached ONLY when such a land exists -> every filter-less board stays byte-identical.
inline bool AnyUntappedFilterSource(const GameState& state)
{
    const int active = state.active_player_index;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && (d->params.is_filter || d->params.ramp_filter)) { return true; }
    }
    return false;
}

// `yield_override` (>= 0) supplies the source's per-tap amount when the caller has the PERMANENT
// (e.g. a storage land's live counter count via PermanentManaYield); -1 falls back to the static
// per-tap yield, keeping every existing caller byte-identical.
inline void AddSourceToPool(ManaPool& pool, const GameState& state, const CardDefinition& def,
                            int yield_override, const Permanent* perm)
{
    // A land Aura's "additional mana" is the AURA's, in the AURA's colour -- so credit it up front,
    // independently of whichever mode the enchanted land itself is in, and then remove it from a
    // caller-supplied yield_override below. PermanentManaYield deliberately FOLDS the aura into the
    // land's per-tap total (the honest number for every yield RANKING -- untap priority, tap order),
    // so crediting it again from that override would invent mana that does not exist. Zero, and
    // therefore inert, for every source with no land aura attached.
    const int aura_bonus = (perm != nullptr) ? LandAuraBonus(state, *perm) : 0;
    if (aura_bonus > 0) { LandAuraAddToPool(pool, state, *perm); }
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
        //
        // FLOATING MANA IS ALSO A FEEDER (opt-out MTG_NO_FLOAT_FEEDS_FILTER) -- the payment path
        // already spends it that way ("use floating if any, else feed one mana from a non-ramp
        // source"), so without this the pool under-credits a board the payer can actually pay from
        // and the cast is pruned before enumeration. Net accounting, so no double count: the
        // floating mana is added once by the caller and this contributes only the +1 net.
        const bool float_feed = FloatFeedsRampFilterEnabled() && FloatLeftoverManaEnabled()
                             && state.floating_mana.Total() > 0;
        if (HasUntappedRampFeeder(state) || float_feed) { ++pool.wild; }
        return;
    }
    // Three Tree City scaled ability: "{2},{T}: add N of a chosen colour", N = creatures you control
    // (any-creature simplification). Modelled like a ramp filter but with a {feeder}-generic feed and
    // a board-count yield: when a feeder can pay the {feeder} and the net (N - feeder) beats the basic
    // {C} tap, this source contributes that net as WILD (the colour is search-chosen). Otherwise it
    // falls through to its basic "{T}: Add {C}" produces below. Empty subtype -> never taken here.
    if (IsScaledManaLand(def))
    {
        if (int net = ScaledManaNetYield(state, def)) { pool.wild += net; return; }
    }
    // Untap-land burst (Wirewood Lodge): one tap is worth (best scaled-Elf yield - 1) of the feed
    // colour when a live 2+ target exists (see UntapBurstBestYield's net-cancellation model). That
    // strictly dominates the plain "{T}: Add {C}" credit (same count at yield 2, but coloured), so
    // it REPLACES the {C} -- crediting both would promise a mana the one tap cannot make. Falls
    // through to the plain credit when the burst is dead (no target, yield < 2, or the flag off).
    if (def.params.untap_creature_cost.has_value())
    {
        if (int net = UntapLandBurstNet(state, state.active_player_index, def))
        { pool.Add(*UntapBurstFeedColor(def), net); return; }
    }
    // The aura's share is already in the pool (above), so take it back out of the override.
    int amt = (yield_override >= 0) ? (yield_override - aura_bonus) : ManaProducedPerTap(def);
    if (amt < 0) { amt = 0; }
    // Reflecting Pool: its colours are the union of the controller's other lands (empty -> adds
    // nothing, the solo-RP dead case). For every normal source this is the static produces[].
    const std::vector<Color>& prod = EffectiveProduces(state, state.active_player_index, def);
    // Domain source (Faeburrow / Bloom Tender): one mana of EACH colour among your permanents
    // from a single tap -- the yield is the dynamic colour count, ignoring the static amount.
    // Credited as wild like a Karoo (the one-of-each nuance is exact at tap time in tap_source).
    if (def.params.domain_mana) { amt = static_cast<int>(prod.size()); }
    if (prod.size() == 1)      { pool.Add(prod[0], amt); }
    else if (!prod.empty())
    {
        pool.wild += amt;
        // ... and record how much of that wild can also pay a {C} PIP -- only a source whose own
        // modes include colourless can (a Yavimaya Coast's "{T}: Add {C}" mode, not a Fertile
        // Ground's "one mana of any colour"). See ManaPool::wild_c. Inert for every deck whose
        // costs carry no {C} pip.
        if (std::find(prod.begin(), prod.end(), Color::Colorless) != prod.end())
        { pool.wild_c += amt; }
    }
}

// Total mana the controller can still produce THIS instant: every UNTAPPED land/dork/rock plus any
// turn-scoped floating reserve. Mirrors AIEngine::BuildAvailableMana (same source filter + float
// gate) so the human-play FLOOR in SoulfireDig sees the same spare mana the planner did. Colour-
// agnostic total (the Hinata discount reduces GENERIC, which any spare mana can re-pay).
inline int SpareUntappedMana(const GameState& state, int controller)
{
    ManaPool pool;
    int gy_fuel = -1;   // Deathrite fuel: lazily counted, decremented per credited source
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller || p.tapped) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        const bool is_land = (d->tmpl == CardTemplate::BasicLand);
        const bool is_dork = (d->tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield)) || d->params.mana_rock
                          || PaySacSpendableNow(state, p, *d);   // §2a (fresh-hold aware)
        if (!is_land && !is_dork) { continue; }
        // Deathrite: credit at most #graveyard-lands such sources (mirrors AvailableManaPool).
        if (d->params.gy_land_exile_mana)
        {
            if (gy_fuel < 0) { gy_fuel = GraveyardLandFuel(state, controller); }
            if (gy_fuel <= 0) { continue; }
            --gy_fuel;
        }
        AddSourceToPool(pool, state, *d, PermanentManaYield(state, p, *d), &p);
    }
    if (FloatLeftoverManaEnabled()) { pool.AddPool(state.floating_mana); }
    return pool.Total();
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
    // 2) Wild reserve mana covers any remaining COLOUR pip (not {C}). This is LEGITIMATE: `wild`
    // means "one tap of a source that can make more than one colour". Reports #6 and #7 were not
    // about this step -- they were about PRODUCERS putting inflexible mana into `wild` (a ritual
    // with no ritual_float_color; BatchPrepayMainCasts dumping a Sol Ring's {C}{C} in). Counting
    // wild->colour payments here is therefore useless as an assertion: it reads ~1M per 200 healthy
    // Hinata games. The audit lives at the two producers instead (MTG_WILD_PIP_AUDIT).
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

// True iff the active player's hand holds a card of a subtype this reveal land wants
// (e.g. Island/Mountain for Frostboil Snarl) -- i.e. it CAN reveal to enter untapped.
inline bool LandCanReveal(const GameState& state, const CardDefinition& def)
{
    const CardParams& pp = def.params;
    const Player& ap = state.ActivePlayer();
    for (const Card& c : ap.hand)
    {
        const CardDefinition* cdef = CardDatabase::Instance().LookupCached(c);
        const SubtypeSet& subs = cdef ? cdef->card.m_subtypes : c.m_subtypes;
        for (const std::string& cs : subs)
            for (const std::string& want : pp.etb_untap_reveal_subtypes)
                if (cs == want) { return true; }
    }
    return false;
}

// Pure heuristic predicate: would this land enter tapped under autonomous play? Does NOT mutate
// state (no life payment). Call while the card is still in hand (the reveal check scans the hand).
//   - Shock land (etb_pay_life_to_untap): enters untapped iff the AI pays the life -- it does so
//     whenever it can keep at least 1 life AND `allow_pay_life` (mana is actually needed this turn).
//   - Reveal land (etb_untap_reveal_subtypes): enters untapped iff a matching card is in hand.
//   - Otherwise: the plain enters_tapped flag.
inline bool LandWouldEnterTapped(const GameState& state, const CardDefinition& def, bool allow_pay_life = true)
{
    const CardParams& pp = def.params;
    // The pay-the-cost-or-enter-tapped decision is provider-owned (LandEntersUntapped); the base
    // hook returns the engine answer unchanged, so this is byte-identical. It is routed through the
    // PREDICATE (not just the real land drop) because the enumeration prices a plan's mana off this
    // same function -- if the two disagreed, the plan's mana and the realised mana would diverge.
    if (pp.etb_pay_life_to_untap > 0)
    {
        const bool heur = allow_pay_life && state.ActivePlayer().life > pp.etb_pay_life_to_untap;
        return !ResolveProvider(state).LandEntersUntapped(state, def, heur);
    }
    if (!pp.etb_untap_reveal_subtypes.empty())
        return !ResolveProvider(state).LandEntersUntapped(state, def, LandCanReveal(state, def));
    if (pp.fastland_max_other_lands >= 0)
    {
        // Fastland (Razorverge Thicket): enters untapped iff you control <= N other lands. The card
        // being played is still in hand (not yet on the battlefield), so every battlefield land the
        // active player controls is an "other" land.
        int other_lands = 0;
        for (const Permanent& p : state.battlefield)
            if (p.controller_index == state.active_player_index && p.card.IsLand()) { ++other_lands; }
        return other_lands > pp.fastland_max_other_lands;
    }
    if (!pp.checkland_subtypes.empty())
    {
        // Check land (Rootbound Crag): "enters tapped unless you control a Mountain or a Forest" --
        // untapped iff a controlled LAND carries any of the listed land subtypes. Same shared-
        // predicate contract as the fastland branch: enumeration pricing and the real drop agree.
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index != state.active_player_index || !p.card.IsLand()) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            const SubtypeSet& subs = d ? d->card.m_subtypes : p.card.m_subtypes;
            for (const std::string& want : pp.checkland_subtypes)
            {
                for (const std::string& s : subs) { if (s == want) { return false; } }
            }
        }
        return true;
    }
    return pp.enters_tapped;
}

// Is there a real "enter untapped by paying a cost" CHOICE to surface for this land (human play)?
//   - Shock land: yes iff the player can afford the life (life > cost) -- the human may pay even when
//     the mana-needed heuristic wouldn't, or decline even when it would.
//   - Reveal land: yes iff a matching card is in hand (otherwise it is forced tapped).
inline bool LandEntryHasChoice(const GameState& state, const CardDefinition& def)
{
    const CardParams& pp = def.params;
    if (pp.etb_pay_life_to_untap > 0) return state.ActivePlayer().life > pp.etb_pay_life_to_untap;
    if (!pp.etb_untap_reveal_subtypes.empty()) return LandCanReveal(state, def);
    return false;
}

// Apply the on-entry cost for a land the controller chose to enter UNTAPPED. Only a shock land has a
// payable cost (its life); a reveal land pays nothing (revealing is free); a plain land is a no-op.
inline void ApplyLandUntapPayment(GameState& state, const CardDefinition& def)
{
    if (def.params.etb_pay_life_to_untap > 0)
        state.ActivePlayer().life -= def.params.etb_pay_life_to_untap;
}

// Decides whether a land enters tapped and applies any "as this land enters" payment (shock life).
// The autonomous / rollout path -- takes the heuristic (LandWouldEnterTapped) and pays the cost when
// it enters untapped. Human play routes the land drop through PlayLandByName, which consults the
// g_play_land_entry_chooser at the shared call site before paying; this helper stays byte-identical
// for the search and every other caller. Returns true if the land enters tapped.
inline bool LandEntersTapped(GameState& state, const CardDefinition& def, bool allow_pay_life = true)
{
    bool tapped = LandWouldEnterTapped(state, def, allow_pay_life);
    if (!tapped) { ApplyLandUntapPayment(state, def); }
    return tapped;
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
    // on top (below the ordered ones, in look order). Same for Mirri's Guile (ReorderNoShuffle).
    if (kind == LookKind::Reorder || kind == LookKind::ReorderNoShuffle)
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
    if (kind == LookKind::Reorder || kind == LookKind::ReorderNoShuffle)
    {
        // Mirri's Guile (ReorderNoShuffle) has no shuffle mode -- order wanted-first regardless.
        const bool do_shuffle = kind == LookKind::Reorder
                             && ((keep_decision == 0)
                                 || (keep_decision == -1 && !ResolveProvider(state).KeepReorderTop(state, looked)));
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

// SEARCH-SCRIPTED disposition (see docs/design/searched-scry-disposition.md). A scry/surveil/reorder
// resolves INLINE, deep inside a land ETB or a spell resolution, so the plan search cannot branch on
// it by enumerating actions -- it has to pin the choice and re-run the apply, exactly as bp_choice
// pins a breakpoint continuation. This thread-local is that pin: >= 0 means "take candidate k of
// TopDispositionCandidates at the NEXT look", -1 (the default) means the provider heuristic decides
// at resolution, which is byte-identical to the pre-branch engine.
//
// Consumed-once by design: the look that reads it resets it, so a plan carrying one scripted choice
// scripts exactly one look and any further look in the same apply falls back to the heuristic. That
// is the same "branch on the first divergence, default for the tail" shape the breakpoint waves and
// the cleanup discard use.
extern thread_local int g_scripted_top_choice;

// Searched ETB-DIG pick (Acclaimed Contender). Same shape as g_scripted_top_choice: an index into
// the provider's ranked EtbDigCandidates list, pinned by the plan for the apply and consumed by the
// first dig, so a plan carrying one scripts exactly one dig and any later dig in the same apply
// falls back to the provider's ranked default. `k < 0` is inert (heuristic).
//
// This exists because the base rule -- the FIRST legal match in look order, i.e. library order --
// is an arbitrary pick, and the choice is live almost every time: MTG_ETBDIG_TRACE over 200 Knights
// games measured 94% of digs with 2+ legal matches (mean 2.6). Which Knight you want depends on
// board and curve, so it is a search decision, not a ranking problem.
extern thread_local int g_scripted_etbdig_choice;

// Scoped pin, mirroring ScriptedTopChoice (restores on exit so a nested apply cannot leak its
// script into the outer one).
struct ScriptedEtbDig
{
    explicit ScriptedEtbDig(int k) : saved(g_scripted_etbdig_choice) { g_scripted_etbdig_choice = k; }
    ~ScriptedEtbDig() { g_scripted_etbdig_choice = saved; }
    ScriptedEtbDig(const ScriptedEtbDig&) = delete;
    ScriptedEtbDig& operator=(const ScriptedEtbDig&) = delete;
    int saved;
};

// Searched HOLD-vs-TAP of the mana creatures (Plan::tapmode_choice, UnprunedGate::TapReserve).
// 0 (default) == the shipped heuristic: reserve every mana creature for the whole turn and sort
// them to the back of the tap backtracker's candidate list. 1 == spend them like any other source.
//
// This is the axis the static rules could not be: whether a body is worth more untapped than the
// mana it makes is a per-LINE question (does anything pump it, does it swing for damage, is the
// alternative source finite), and every static encoding of it measured worse than the blanket rule
// (see the rejected table in docs/design/mana-source-reservation.md). So make it a branch and let
// the rollout score both, with the blanket rule as the branch's DEFAULT.
//
// UNLIKE the etbdig/tutor pins this is NOT consumed by its first reader: it must hold for every
// payment in the apply, so the scope is the whole plan. Not-reserving is strictly MORE permissive
// than reserving (the hold only ever removes sources from a solve), so a variant can never make an
// enumerated plan unexecutable -- it only changes which sources end up tapped.
extern thread_local int g_scripted_tapmode;

struct ScriptedTapMode
{
    explicit ScriptedTapMode(int k) : saved(g_scripted_tapmode) { g_scripted_tapmode = k; }
    ~ScriptedTapMode() { g_scripted_tapmode = saved; }
    ScriptedTapMode(const ScriptedTapMode&) = delete;
    ScriptedTapMode& operator=(const ScriptedTapMode&) = delete;
    int saved;
};

// Whole-plan pin for the fresh-spend branch (Plan::freshmode_choice; declared with FreshHoldActive
// above). Same shape as ScriptedTapMode: static for the whole apply, never consumed, restores on
// exit so a nested apply cannot leak its script into the outer one.
struct ScriptedFreshMode
{
    explicit ScriptedFreshMode(int k) : saved(g_scripted_freshmode) { g_scripted_freshmode = k; }
    ~ScriptedFreshMode() { g_scripted_freshmode = saved; }
    ScriptedFreshMode(const ScriptedFreshMode&) = delete;
    ScriptedFreshMode& operator=(const ScriptedFreshMode&) = delete;
    int saved;
};

// Searched TUTOR pick by index (Plan::tutor_choice, MTG_TUTOR_AXIS_RESOLVE=1): k >= 0 makes the
// next tutor resolution take the k-th candidate of the provider ranking computed AT THAT
// RESOLUTION -- i.e. on the true mid-plan state -- instead of the front. Consumed by the first
// tutor of the apply, mirroring g_scripted_etbdig_choice exactly (a second tutor keeps the
// provider's default, matching the name axis's vary-ONE-tutor rule).
extern thread_local int g_scripted_tutor_choice;

// Scoped pin, mirroring ScriptedEtbDig (restores on exit so a nested apply cannot leak its
// script into the outer one).
struct ScriptedTutor
{
    explicit ScriptedTutor(int k) : saved(g_scripted_tutor_choice) { g_scripted_tutor_choice = k; }
    ~ScriptedTutor() { g_scripted_tutor_choice = saved; }
    ScriptedTutor(const ScriptedTutor&) = delete;
    ScriptedTutor& operator=(const ScriptedTutor&) = delete;
    int saved;
};

// Searched SAC-LAND target by index (Plan::sac_pins, MTG_SAC_AXIS): a per-ordinal pin LIST
// rather than a single consumable int, because one plan can pay several sacrifice-a-land costs
// and the winning deviation can be the second (cg30). PerformSacrificeLandCost consumes one
// entry per call in canonical execution order; entry k >= 0 takes ranked[min(k, size-1)]
// (duplicate-not-whiff clamp), entry -1 keeps the provider's front, and a call past the list's
// end (a breakpoint continuation's extra sacrifice) also keeps the front. The list rides a
// POINTER to the Plan's own vector (the Plan outlives the apply), so installing the pin never
// copies.
extern thread_local const std::vector<int>* g_scripted_sac_pins;
extern thread_local int g_scripted_sac_cursor;

// Scoped pin, mirroring ScriptedTutor (restores list AND cursor on exit so a nested apply
// cannot leak its script into the outer one -- and so the outer apply resumes at its own
// consumption point).
struct ScriptedSacLand
{
    explicit ScriptedSacLand(const std::vector<int>& pins)
        : saved(g_scripted_sac_pins), saved_cursor(g_scripted_sac_cursor)
    {
        g_scripted_sac_pins   = pins.empty() ? nullptr : &pins;
        g_scripted_sac_cursor = 0;
    }
    ~ScriptedSacLand() { g_scripted_sac_pins = saved; g_scripted_sac_cursor = saved_cursor; }
    ScriptedSacLand(const ScriptedSacLand&) = delete;
    ScriptedSacLand& operator=(const ScriptedSacLand&) = delete;
    const std::vector<int>* saved;
    int saved_cursor;
};

// The ONE consumption rule for the sac-pin list, shared by every sacrifice-a-land site
// (PerformSacrificeLandCost for search/rollout/plan-executor; AIEngine's cast-path site) --
// two open-coded copies of one sacrifice rule is exactly the lockstep hole cg30 exposed.
// Returns this call's ordinal pin (-1 = provider front), advancing the cursor; a call past the
// list's end returns -1.
inline int ConsumeScriptedSacPin()
{
    if (g_scripted_sac_pins != nullptr
        && g_scripted_sac_cursor < static_cast<int>(g_scripted_sac_pins->size()))
    {
        const int pin = (*g_scripted_sac_pins)[g_scripted_sac_cursor];
        ++g_scripted_sac_cursor;
        return pin;
    }
    return -1;
}

// MTG_FB_TRACE diagnostic only (no play change): how many firebreathing activations the CURRENT
// turn's combat paid for. Reset at every Firebreathe call; read by GameEngine::MainPhase to detect
// a post-combat main that casts on a turn that pumped -- the one situation in which the pump pool's
// "read, never tap" shortcut would double-spend mana. See docs/design/engine-heuristics-to-providers.md.
extern thread_local int g_fb_activations_this_turn;

// Scoped pin. Restores the previous value on exit, so a nested apply (a breakpoint re-solve inside
// an apply) cannot leak its script into the outer one. `k < 0` is inert.
struct ScriptedTopChoice
{
    explicit ScriptedTopChoice(int k) : saved(g_scripted_top_choice) { g_scripted_top_choice = k; }
    ~ScriptedTopChoice() { g_scripted_top_choice = saved; }
    ScriptedTopChoice(const ScriptedTopChoice&) = delete;
    ScriptedTopChoice& operator=(const ScriptedTopChoice&) = delete;
    int saved;
};

// The candidate dispositions this look may take, HEURISTIC FIRST. Index 0 is exactly what
// HeuristicTopDisposition returns, so a k=1 enumeration is byte-identical to no branch at all; the
// rest are the alternatives the search may score. Lives here (not on DecisionProvider) only because
// TopDisposition is defined here -- the RANKING inside it is provider-owned via ScryKeepOnTop /
// SituationalCardRank, and a provider that wants a different candidate SET should override
// ScryKeepOnTop rather than reorder this list.
inline std::vector<TopDisposition> TopDispositionCandidates(const GameState& state,
                                                            const std::vector<Card>& looked,
                                                            LookKind kind, int keep_decision = -1);

// Resolve one look's disposition: scripted pin first (search), then the human chooser, then the
// provider heuristic. Shared by ScryTop / SurveilTop / ReorderTopOrShuffle so all three branch
// identically.
inline TopDisposition ChooseTopDisposition(const GameState& state, const std::string& source,
                                           const std::vector<Card>& looked, LookKind kind,
                                           int keep_decision = -1)
{
    if (g_scripted_top_choice >= 0)
    {
        const int k = g_scripted_top_choice;
        g_scripted_top_choice = -1;                    // consumed by this look
        std::vector<TopDisposition> cands = TopDispositionCandidates(state, looked, kind, keep_decision);
        if (!cands.empty())
        {
            return cands[static_cast<std::size_t>(k) < cands.size() ? k : cands.size() - 1];
        }
    }
    if (g_play_top_chooser) { return (*g_play_top_chooser)(state, source, looked, kind); }
    return HeuristicTopDisposition(state, looked, kind, keep_decision);
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
    if (kind == LookKind::Reorder || kind == LookKind::ReorderNoShuffle)
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
        // "You may shuffle" exists only for the Ponder family; Mirri's Guile cannot shuffle.
        if (kind == LookKind::Reorder)
        {
            TopOption sh; sh.disp.shuffle = true; sh.label = "Shuffle them away";
            opts.push_back(std::move(sh));
        }
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

// The heuristic pick first, then every OTHER legal disposition in EnumerateTopDispositions order.
// Deduped against the heuristic by top_order + shuffle so candidate 0 is never repeated later.
inline std::vector<TopDisposition> TopDispositionCandidates(const GameState& state,
                                                            const std::vector<Card>& looked,
                                                            LookKind kind, int keep_decision)
{
    std::vector<TopDisposition> out;
    out.push_back(HeuristicTopDisposition(state, looked, kind, keep_decision));
    // COPY, not a reference: the loop below push_backs into `out`, which reallocates and would
    // leave a reference to out.front() dangling -- and the dedup then compares against freed
    // memory. When the stale read happens to differ, the `continue` misfires and the heuristic
    // candidate is emitted a SECOND time, so the search explores a candidate list it should not
    // have (a silent, allocator-dependent change to the plan set, not just a cosmetic one).
    // Caught by ASAN 2026-08-25 (heap-use-after-free, READ of size 1 = h.shuffle). A TopDisposition
    // is a bool + a small vector<int>; copying it once per call is negligible next to the
    // permutation enumeration it guards.
    const TopDisposition h = out.front();
    for (const TopOption& o : EnumerateTopDispositions(kind, looked))
    {
        if (o.disp.shuffle == h.shuffle && o.disp.top_order == h.top_order) { continue; }
        out.push_back(o.disp);
    }
    return out;
}

// NARROWED reorder candidates (Ponder). TopDispositionCandidates above returns the heuristic plus
// EVERY permutation -- 6 orderings + shuffle = 7 for a 3-card look -- and searching all of them is
// mostly waste, because Ponder DRAWS immediately after reordering: only the card placed on TOP is
// received now. Positions 2 and 3 affect the draws one and two turns later, which a ~5.7-turn
// average game frequently never reaches.
//
// So this returns, heuristic FIRST (index 0 == no branch, byte-identical):
//   * the heuristic disposition,
//   * the shuffle (unless the heuristic already shuffles),
//   * one variant per DISTINCT top card, the remainder following in the heuristic's relative order.
// That is m + 1 candidates instead of m! + 1 -- 4 instead of 7 at m = 3 -- and it keeps every
// option that differs in the card actually drawn.
inline std::vector<TopDisposition> ReorderCandidatesNarrow(const GameState& state,
                                                          const std::vector<Card>& looked,
                                                          int keep_decision = -1)
{
    std::vector<TopDisposition> out;
    out.push_back(HeuristicTopDisposition(state, looked, LookKind::Reorder, keep_decision));
    const int m = static_cast<int>(looked.size());
    if (m <= 0) { return out; }
    // The heuristic's ordering, used as the tail order for every top-card variant so the variants
    // differ ONLY in what is drawn next.
    std::vector<int> base = out.front().top_order;
    for (int i = 0; i < m; ++i)
    { if (std::find(base.begin(), base.end(), i) == base.end()) { base.push_back(i); } }
    if (!out.front().shuffle)
    {
        TopDisposition sh; sh.shuffle = true;
        out.push_back(sh);
    }
    const int heur_top = out.front().shuffle ? -1 : (base.empty() ? -1 : base.front());
    for (int t = 0; t < m; ++t)
    {
        if (t == heur_top) { continue; }
        TopDisposition d;
        d.top_order.push_back(t);
        for (int i : base) { if (i != t) { d.top_order.push_back(i); } }
        out.push_back(std::move(d));
    }
    return out;
}

// Searched Ponder-style REORDER disposition: an index into ReorderCandidatesNarrow, pinned by the
// plan and consumed by the first reorder. DELIBERATELY SEPARATE from g_scripted_top_choice: that pin
// is consumed by the FIRST look of any kind, so a turn that plays an ETB-scry land AND casts Ponder
// would have the land eat a pin meant for the Ponder. Two pins, two consumers, no collision.
extern thread_local int g_scripted_reorder_choice;

struct ScriptedReorder
{
    explicit ScriptedReorder(int k) : saved(g_scripted_reorder_choice) { g_scripted_reorder_choice = k; }
    ~ScriptedReorder() { g_scripted_reorder_choice = saved; }
    ScriptedReorder(const ScriptedReorder&) = delete;
    ScriptedReorder& operator=(const ScriptedReorder&) = delete;
    int saved;
};

// Scry N (e.g. Temple of Epiphany): look at the top N cards and bottom the unwanted ones using a
// deck-aware heuristic (HeuristicTopDisposition), then keep the rest on top in look order. Under
// --claude-play g_play_top_chooser is set (and RevealLogPause nulls it during the search, so this
// is REAL resolution only) and the human picks the disposition instead. Byte-identical for search
// unless a plan pinned a scripted choice (g_scripted_top_choice).
inline void ScryTop(GameState& state, int n, const std::string& source = "Scry")
{
    Player& ap = state.ActivePlayer();
    const int look = std::min(n, static_cast<int>(ap.library.size()));
    if (look <= 0) { return; }

    std::vector<Card> looked(ap.library.begin(), ap.library.begin() + look);
    TopDisposition disp = ChooseTopDisposition(state, source, looked, LookKind::Scry);

    // Reveal capture (real play only; null during search): seen cards in look order, and which
    // were kept on top vs bottomed -- read from the chosen disposition BEFORE looked is consumed.
    if (RevealVisible())
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
        EmitReveal(state.turn_number, source, seen_nums, seen_names, kept_nums, bottom_nums);
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
    //
    // The searched ORDER axis pins its own index (see ScriptedReorder), consumed here so it cannot
    // be eaten by an earlier scry look in the same plan. Index 0 is the heuristic, so an unpinned
    // plan is byte-identical.
    TopDisposition disp;
    if (g_scripted_reorder_choice >= 0)
    {
        const int k = g_scripted_reorder_choice;
        g_scripted_reorder_choice = -1;              // consumed by this reorder
        std::vector<TopDisposition> cands = ReorderCandidatesNarrow(state, looked, keep_decision);
        disp = cands[static_cast<std::size_t>(k) < cands.size() ? k : cands.size() - 1];
    }
    else
    {
        disp = ChooseTopDisposition(state, source, looked, LookKind::Reorder, keep_decision);
    }

    if (RevealVisible())
    {
        std::vector<int> seen_nums; std::vector<std::string> seen_names;
        for (const Card& c : looked) { seen_nums.push_back(c.m_number); seen_names.push_back(c.m_name); }
        if (disp.shuffle)
        {
            EmitReveal(state.turn_number, source + " (shuffle)", seen_nums, seen_names,
                                       /*kept*/ std::vector<int>{}, /*bottomed*/ seen_nums);
        }
        else
        {
            // Final top order = ordered indices, then any unordered ones (Ponder keeps all on top).
            std::vector<int> kept_nums; std::vector<char> placed(look, 0);
            for (int idx : disp.top_order)
            { if (idx >= 0 && idx < look && !placed[idx]) { placed[idx] = 1; kept_nums.push_back(looked[idx].m_number); } }
            for (int i = 0; i < look; ++i) { if (!placed[i]) { kept_nums.push_back(looked[i].m_number); } }
            EmitReveal(state.turn_number, source, seen_nums, seen_names, kept_nums, /*bottomed*/ std::vector<int>{});
        }
    }

    for (int _e = 0; _e < look; ++_e) { ap.library.erase(ap.library.begin()); }
    ApplyTopDisposition(state, looked, disp, LookKind::Reorder);
}

// Mirri's Guile: "At the beginning of your upkeep, you may look at the top three cards of your
// library, then put them back in any order." NO shuffle, NO bottoming (LookKind::ReorderNoShuffle).
// "May" always taken (free information + ordering is weakly dominant with the wanted-first
// heuristic). Human play routes through the same top chooser as scry/reorder; the scripted search
// pin is deliberately NOT consulted (this fires at the turn transition, outside plan applies).
inline void ReorderTopNoShuffle(GameState& state, int n, const std::string& source)
{
    Player& ap = state.ActivePlayer();
    const int look = std::min(n, static_cast<int>(ap.library.size()));
    if (look <= 1) { return; }
    std::vector<Card> looked(ap.library.begin(), ap.library.begin() + look);

    TopDisposition disp;
    if (g_play_top_chooser)
    { disp = (*g_play_top_chooser)(state, source, looked, LookKind::ReorderNoShuffle); }
    else
    { disp = HeuristicTopDisposition(state, looked, LookKind::ReorderNoShuffle); }
    disp.shuffle = false;   // no shuffle mode exists on this card

    if (RevealVisible())
    {
        std::vector<int> seen_nums; std::vector<std::string> seen_names;
        for (const Card& c : looked) { seen_nums.push_back(c.m_number); seen_names.push_back(c.m_name); }
        std::vector<int> kept_nums; std::vector<char> placed(look, 0);
        for (int idx : disp.top_order)
        { if (idx >= 0 && idx < look && !placed[idx]) { placed[idx] = 1; kept_nums.push_back(looked[idx].m_number); } }
        for (int i = 0; i < look; ++i) { if (!placed[i]) { kept_nums.push_back(looked[i].m_number); } }
        EmitReveal(state.turn_number, source, seen_nums, seen_names, kept_nums,
                   /*bottomed*/ std::vector<int>{});
    }

    for (int _e = 0; _e < look; ++_e) { ap.library.erase(ap.library.begin()); }
    ApplyTopDisposition(state, looked, disp, LookKind::ReorderNoShuffle);
}

// Upkeep-reorder trigger sweep (Mirri's Guile). One reorder per permanent carrying the param
// (a second copy's reorder is a heuristic no-op but stays faithful). Called from BOTH upkeep
// sites (GameEngine::UpkeepStep + the rollout's turn transition) -- lockstep.
inline void PerformUpkeepReorder(GameState& state)
{
    const int active = state.active_player_index;
    // Collect first: the reorder itself never changes the battlefield, but keep the scan cheap.
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d || d->params.upkeep_reorder <= 0) { continue; }
        ReorderTopNoShuffle(state, d->params.upkeep_reorder, p.card.m_name.str());
    }
}

// ResolveExpressiveIteration -- body in SpellEffects.cpp (see the header note above).
void ResolveExpressiveIteration(GameState& state);

// BounceKarooLand -- body in SpellEffects.cpp (see the header note above).
void BounceKarooLand(GameState& state, int controller, int self_index);

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
    TopDisposition disp = ChooseTopDisposition(state, source, looked, LookKind::Surveil);

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
        EmitReveal(state.turn_number, source, seen_nums, seen_names, kept_nums, grave_nums);
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
    // "When this land enters, you get {E}" (Aether Hub). Added here as well as in the land DROP
    // (LandPlay.cpp) because this is the separate FETCH entry path -- a fetched land skips
    // PlayLandFromHand entirely. Unreachable in this deck (no fetchlands), but the divergence is
    // real and pre-existing for etb_lifegain / etb_bounce_land / the rad mode, so at least do not
    // grow it.
    if (def.params.etb_energy > 0)
    { state.players[state.active_player_index].energy_counters += def.params.etb_energy; }
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
    // "Sacrifice this land" is the OTHER half of the activation cost, and it is a real sacrifice
    // even though this engine collapses the fetchland's own battlefield stay to nothing (see the
    // card's bracket note: it never enters, so it never taps). A sacrifice-watcher must still see
    // it -- in a real game the fetchland is on the battlefield and is sacrificed, so cracking a
    // Bloodstained Mire pumps a Slaughter-Priest of Mogis. Fired here, at the cost, before the
    // search resolves. Param-gated -> byte-identical for every deck with no watcher in play.
    FireSacrificeWatchers(state, controller_index);

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
    // Searching the library shuffles it (CR 701.19). Deterministic + lockstep, and ON BY
    // DEFAULT -- SearchShuffleEnabled() is !EnvOn("MTG_NO_SEARCH_SHUFFLE"), so the opt-OUT is
    // MTG_NO_SEARCH_SHUFFLE. (There is no MTG_SEARCH_SHUFFLE opt-in; comments claiming this is
    // inert by default were stale and inverted the real default.)
    ShuffleAfterSearch(state, controller_index);
}

// Sacrifice-a-land ADDITIONAL COST resolution (Shard Volley / Crop Rotation) for the
// rollout + ApplyPlan-executor path (AIEngine's autonomous cast path has its own pre-resolution
// site). WHICH land is provider-owned (SacrificeLandCandidates: tapped-first, the historical
// rule) with the human-play chooser override (g_play_sacrifice_chooser, nulled for search/
// rollout scopes). For a tutor_land_to_battlefield spell (Crop Rotation) the caller MUST run
// this BEFORE the search puts the fetched land onto the battlefield -- additional costs are paid
// at cast time, before resolution (CR 601.2h), so the just-fetched land is never a legal
// sacrifice target (2026-08-06 claude-play sweep flag, seeds 9012/9015).
inline void PerformSacrificeLandCost(GameState& state, const std::string& spell_name)
{
    // Consume this call's ordinal from the searched pin list (Plan::sac_pins) FIRST,
    // unconditionally -- even a sacrifice that finds no land holds its ordinal, so the plan's
    // j-th entry always steers the plan's j-th sacrifice.
    const int pin = ConsumeScriptedSacPin();
    Player& ap = state.players[state.active_player_index];
    std::vector<int> lands;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != state.active_player_index || !p.card.IsLand()) { continue; }
        lands.push_back(i);
    }
    int idx = -1;
    if (!lands.empty())
    {
        const std::vector<int> ranked = ResolveProvider(state).SacrificeLandCandidates(
            state, state.active_player_index, lands);
        if (!ranked.empty())
        {
            // pin >= 0 takes the k-th candidate of the ranking computed at THIS resolution
            // state, clamped to the last (duplicate-not-whiff, as the tutor axis); -1 = front.
            const std::size_t k = pin < 0 ? 0
                : std::min<std::size_t>(static_cast<std::size_t>(pin), ranked.size() - 1);
            idx = ranked[k];
        }
    }
    if (g_play_sacrifice_chooser && lands.size() > 1 && idx >= 0)
    {
        int def_opt = 0;
        for (int k = 0; k < static_cast<int>(lands.size()); ++k)
        { if (lands[k] == idx) { def_opt = k; break; } }
        int chosen = (*g_play_sacrifice_chooser)(state, state.active_player_index, spell_name,
                                                 lands, def_opt);
        if (chosen >= 0 && chosen < static_cast<int>(lands.size())) { idx = lands[chosen]; }
    }
    if (idx >= 0)
    {
        ap.graveyard.push_back(state.battlefield[idx].card);
        state.battlefield.erase(state.battlefield.begin() + idx);
        FireSacrificeWatchers(state, state.active_player_index);   // Slaughter-Priest
    }
}

// Battlefield indices of the controller's creatures legal for a "sacrifice a <color> creature"
// additional cost (Natural Order: green), most-expendable-first: tokens before real cards, then
// lowest effective power, then battlefield order. Colour read off the live card (a token copy /
// coloured token carries its colour; plain tokens are colourless and only match an empty filter).
inline std::vector<int> SacCreatureCandidateIndices(const GameState& state, int controller,
                                                    const std::string& color)
{
    std::vector<int> out;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != controller || !p.card.IsCreature()) { continue; }
        if (!CardHasColorNamed(p.card, color)) { continue; }
        out.push_back(i);
    }
    // MTG_SAC_SPARE_ATTACKERS (A/B lever, default OFF): rank victims by what this turn's ATTACK
    // actually loses, not by printed size. USER (2026-08-21): "We should sac summoning sick
    // worldspine wurms ... That's a good choice to get more critters." A summoning-sick token
    // SPAWNER (dies_trigger_creates_tokens > 0) is the best victim of all: it contributes zero to
    // this attack (Craterhoof's pump grants no team haste) and its death is a net BODY GAIN --
    // Worldspine out, three 5/5s in, each one more count for a pump's X. Next, prefer any victim
    // that CANNOT attack right now (tapped for the turn's already-made payments, or sick) over one
    // that can -- by Natural Order's cast the batch prepay has run, so a mana-tapped elf is free
    // where an untapped one is a pumped attacker (measured: one 1/1 at hoof-pump X=7 is the whole
    // 27-vs-19 margin on d0 gi106). Default order unchanged: tokens, then lowest power.
    static const bool spare_attackers = EnvOn("MTG_SAC_SPARE_ATTACKERS");
    std::stable_sort(out.begin(), out.end(), [&](int a, int b)
    {
        const Permanent& pa = state.battlefield[a];
        const Permanent& pb = state.battlefield[b];
        if (spare_attackers)
        {
            auto tier = [&](const Permanent& p) -> int
            {
                const bool atk = CanAttackFull(p, state.battlefield, p.controller_index);
                const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
                if (!atk && d && d->params.dies_trigger_creates_tokens > 0) { return 0; }
                if (p.is_token)                                             { return 1; }
                if (!atk)                                                   { return 2; }
                return 3;
            };
            const int ta = tier(pa), tb = tier(pb);
            if (ta != tb) { return ta < tb; }
            return pa.EffectivePower() < pb.EffectivePower();
        }
        if (pa.is_token != pb.is_token) { return pa.is_token; }
        return pa.EffectivePower() < pb.EffectivePower();
    });
    return out;
}

// "As an additional cost to cast this spell, sacrifice a <color> creature" (Natural Order). Paid
// at CAST time (CR 601.2h) in both worlds, BEFORE resolution -- so a sacrificed Worldspine Wurm's
// dies-triggers (3 Wurm tokens + shuffle-into-library) resolve before the search, making the Wurm
// itself a legal fetch target. `victim_id` is the searched plan variant (card m_number); <= 0 or
// stale falls back to the most-expendable candidate. Fires the full death cascade.
inline void PerformSacrificeCreatureCost(GameState& state, const std::string& spell_name,
                                         const std::string& color, int victim_id)
{
    const int active = state.active_player_index;
    int idx = -1;
    if (victim_id > 0)
    {
        for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
        {
            const Permanent& p = state.battlefield[i];
            if (p.controller_index == active && p.card.m_number == victim_id
                && p.card.IsCreature() && CardHasColorNamed(p.card, color)) { idx = i; break; }
        }
    }
    if (idx < 0)
    {
        std::vector<int> cands = SacCreatureCandidateIndices(state, active, color);
        if (!cands.empty()) { idx = cands.front(); }
    }
    if (idx < 0) { return; }   // no legal victim (enumeration should have gated the cast)
    const Card dead    = state.battlefield[idx].card;
    const bool was_tok = state.battlefield[idx].is_token;
    state.players[state.battlefield[idx].owner_index].graveyard.push_back(dead);
    state.battlefield.erase(state.battlefield.begin() + idx);
    if (g_play_event_sink && !g_tap_speculating)
    {
        EmitPlayEvent(state.turn_number, "sacrifice",
                      "\xF0\x9F\x94\xAA " + spell_name + " -- sacrificed " + dead.m_name.str());
    }
    FireSacrificeWatchers(state, active);   // Slaughter-Priest (a sac cost IS a sacrifice)
    OnCreatureDies(state, active, dead, was_tok);
}

// Crop Rotation resolution (CardParams::tutor_land_to_battlefield): "Search your library for a
// land card, put that card onto the battlefield, then shuffle." Like PerformFetch but WITHOUT the
// 1-life fetch payment, with the tutor axis choosing the target (target_name = the searched
// StackEntry::tutor_target; empty -> the provider's TutorCandidates front pick), and with the
// Forbidden Orchard on-play hook mirrored: a fetched Orchard is tapped for mana this same turn, so
// it spawns the opponent's Spirit on entry exactly like LandPlay's freshly-played copy (scoped
// HERE, not in EnterLand, so existing fetchland behaviour stays byte-identical -- basic-typed
// fetches can never pull an Orchard). Shared by the executor (EffectHandler) and the rollout
// (TurnSolver::ApplyPlanDirect), lockstep.
inline void PerformLandTutorToBattlefield(GameState& state, int controller_index,
                                          const CardParams& pp, const std::string& target_name = "")
{
    Player& ap = state.players[controller_index];

    std::string want = target_name;
    if (want.empty())
    {
        std::vector<std::string> cands =
            ResolveProvider(state).TutorCandidates(state, controller_index, pp);
        if (cands.empty()) { return; }   // whiff: no land left in the library
        want = cands.front();
    }
    int idx = -1;
    for (int i = 0; i < static_cast<int>(ap.library.size()); ++i)
    {
        if (ap.library[i].m_name == want) { idx = i; break; }
    }
    if (idx < 0) { return; }   // chosen target no longer present (search/real drift guard)
    Card lc = ap.library[idx];
    if (!lc.IsLand())
    {
        const CardDefinition* pd = CardDatabase::Instance().LookupCached(lc);
        if (!pd || !pd->card.IsLand()) { return; }   // guard: only a land card may be put
    }
    ap.library.erase(ap.library.begin() + idx);
    const CardDefinition* def = CardDatabase::Instance().LookupCached(lc);
    if (def)
    {
        EnterLand(state, *def, lc.m_number);
        if (IsForbiddenOrchard(def)) { SpawnOpponentSpirit(state); }
    }
    else
    {
        Permanent perm;
        perm.card              = lc;
        perm.controller_index  = controller_index;
        perm.owner_index       = controller_index;
        perm.entered_this_turn = true;
        state.battlefield.push_back(perm);
    }
    // "...then shuffle" (CR 701.19); deterministic + lockstep. ON BY DEFAULT (opt-OUT is
    // MTG_NO_SEARCH_SHUFFLE).
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

// Live depletion counters on a permanent (0 for a non-depletion source). Used by the scarcity
// greedy's within-tier tiebreak -- more counters tap FIRST (see DepletionTapOrderEnabled).
inline int DepletionCountersOn(const Permanent& source)
{
    for (const Counter& ctr : source.counters)
    {
        if (ctr.type == Counter::Type::Depletion) { return ctr.count; }
    }
    return 0;
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
    // Namespace-scope (not function-local): a function-local `static const bool` forces a
    // thread-safe init-guard check on EVERY call, which the callgrind of a token-heavy rollout
    // showed as ~0.9% self-cost (called once per backtracker node, 39.8M x). An inline variable
    // is initialized once at static-init and read with no guard -- byte-identical value, zero
    // per-node overhead. Read only at runtime during payments, so no static-init-order hazard.
    inline const bool g_enabled = EnvOn("MTG_TAP_STATS");
    inline bool Enabled() { return g_enabled; }
    inline std::atomic<std::uint64_t> g_backtrack_entries{0};
    inline std::atomic<std::uint64_t> g_nodes{0};          // ALL recursion nodes (top-level + deep)
    inline std::atomic<std::uint64_t> g_top_memo_off{0};   // top-level calls that ran with NO fail-memo
    inline std::atomic<std::uint64_t> g_max_n{0};          // max battlefield size seen at a top-level call
    // Max SOURCE-list size at a top-level call. The pair (max_n, max_src) is the whole argument for
    // indexing the memo by source position: on the boards that disabled the memo these differ by 3x
    // (Mirrorwing measured n=105 against 33 sources -- 63 of the 105 were Treasure tokens, which are
    // sac-for-mana ACTIONS and never enter the source list at all).
    inline std::atomic<std::uint64_t> g_max_src{0};
    // CALL-SITE ATTRIBUTION (2026-08-16, docs/design/fivecolour-payment-query-fold.md step 0).
    // Every top-level backtracker entry comes from exactly one of four sites. Which one dominates
    // decides the whole FiveColour tractability plan: per-PLAN prepay means the payment fold already
    // exists (BatchPrepayMainCasts folds a plan's casts into ONE combined cost) and the lever is
    // search BREADTH; per-CAST fallback means the fold is missing at the enumerator.
    inline std::atomic<std::uint64_t> g_site_prepay_held{0};   // BatchPrepayMainCasts, reserved attempt
    inline std::atomic<std::uint64_t> g_site_prepay_plain{0};  // BatchPrepayMainCasts, unrestricted
    inline std::atomic<std::uint64_t> g_site_percast{0};       // per-cast fallback (greedy failed)
    inline std::atomic<std::uint64_t> g_site_percast_filter{0};// per-cast floating-fed filter retry
    inline std::atomic<std::uint64_t> g_prepay_calls{0};       // BatchPrepayMainCasts invocations
    inline std::atomic<std::uint64_t> g_prepay_declined{0};    // ...that declined (-> per-cast path)
    // OVER-TAP of the whole-turn prepay: how much more mana the accepted solve PRODUCED than the
    // batch's combined cost required. This is the quantity behind the recheck's proven-defect class
    // (docs/design/prepay-payment-path-recheck.md §2a): the surplus used to be laundered as `wild`
    // and so was harmlessly fungible for a later mid-phase cast; now it carries its true colour, and
    // a second segment that needs a different colour finds every land committed and taps the
    // reserved mana creature instead -- which costs the attack, and the game. Over-tap is not always
    // avoidable (a source that makes two mana cannot be half-tapped), so the meter separates the
    // total from the count and from the part that is pure EXCESS SOURCES (over-tap >= the smallest
    // possible granularity, i.e. a source that need not have been touched at all).
    inline std::atomic<std::uint64_t> g_prepay_accepted{0};     // solves that succeeded and pre-loaded
    inline std::atomic<std::uint64_t> g_prepay_overtap_calls{0};// ...with produced > combined
    inline std::atomic<std::uint64_t> g_prepay_overtap_mana{0}; // total surplus mana, summed
    inline std::atomic<std::uint64_t> g_prepay_overtap_srcs{0}; // sources tapped beyond the minimum
    // SHRINK post-pass (MTG_PREPAY_SHRINK): accepted solves it gave at least one source back on,
    // and how many sources in total. The ratio against g_prepay_overtap_calls is the answer to
    // "how much of the over-tap was AVOIDABLE" -- the number that decides whether the prepay's
    // over-commitment is a real defect or an artefact of indivisible two-mana sources.
    inline std::atomic<std::uint64_t> g_prepay_shrunk{0};
    inline std::atomic<std::uint64_t> g_prepay_shrunk_srcs{0};
    // Outcome split of the top-level entries (payable vs unpayable) + the nodes each outcome consumed.
    // Answers: is the backtracker mostly PROVING FAILURE (a byte-identical exact-frontier prune would
    // remove those nodes) or mostly SEARCHING FOR A PAYMENT it does find (only pay-from-frontier or a
    // better greedy would help)? Recorded by the public TapForCostBacktrack wrapper (the recursion calls
    // the worker directly, so every wrapper call is one top-level entry).
    inline std::atomic<std::uint64_t> g_entries_ok{0};
    inline std::atomic<std::uint64_t> g_entries_fail{0};
    inline std::atomic<std::uint64_t> g_nodes_ok{0};
    inline std::atomic<std::uint64_t> g_nodes_fail{0};
    // Flow-prune oracle accounting: entries it proved infeasible up front (pruned, saving their whole
    // subtree) vs entries where it bailed (a source/cost it can't model exactly -> fell through to the
    // backtracker). High bail% on a deck means the exact model needs extending (e.g. bounce lands).
    inline std::atomic<std::uint64_t> g_flow_prune{0};
    inline std::atomic<std::uint64_t> g_flow_bail{0};
    // Identical-sibling collapse (s_dup_of_buf in the worker): candidates skipped because an
    // identical earlier sibling already failed at the same node. Each skip prunes a whole subtree.
    inline std::atomic<std::uint64_t> g_dup_skips{0};
    // PAYABLE MANA CACHE accounting. Added with the scaling-source key fix (2026-08-14): re-enabling
    // the cache under a domain source bought only ~10% on the degenerate FiveColour rollouts, and the
    // question that answers "what next" is WHY -- a cache that is never consulted (skip), never hits
    // (miss), or hits but cannot store its solution (unstorable) each point at a different lever.
    inline std::atomic<std::uint64_t> g_mc_hit{0};
    inline std::atomic<std::uint64_t> g_mc_miss{0};
    inline std::atomic<std::uint64_t> g_mc_skip_shape{0};   // not the canonical batch-prepay shape
    inline std::atomic<std::uint64_t> g_mc_skip_key{0};     // key builder declined (a legacy bail-out)
    inline std::atomic<std::uint64_t> g_mc_unstorable{0};   // solved, but a tapped source can't replay
    // WHERE THE DFS NODES GO, split by the cache's reach. A hit costs 0 nodes, so every node belongs to
    // either a MISS (inside the cache's reach, just not memoised yet) or a SKIP (a call shape the cache
    // does not cover at all). Answers "if the cache handles payment, why is payment still expensive?"
    //
    // RUN THESE SINGLE-THREADED (--threads 1). Every per-entry node figure here -- and the pre-existing
    // g_nodes_ok/g_nodes_fail split above -- is a DELTA on the process-wide g_nodes taken around one
    // worker call, so under N threads it also counts whatever the other N-1 threads did meanwhile. The
    // tell is the buckets summing past 100% of `nodes` (measured 122.9% at 12 threads, exactly 100.0%
    // at 1). Ratios between two buckets survive the inflation roughly; absolute shares do not.
    inline std::atomic<std::uint64_t> g_mc_nodes_miss{0};
    inline std::atomic<std::uint64_t> g_mc_nodes_skip{0};
    // FAILURE-MEMO BUCKET RETENTION. The thread_local memo is emptied with clear(), which RETAINS the
    // bucket array (deliberately -- no per-call bucket alloc) but also memsets it, and the array never
    // shrinks. So a single degenerate payment that inflates the table taxes every LATER cheap payment
    // on that thread for the rest of the run. These say whether that is actually happening: the largest
    // array ever retained, and the split of top-level calls that paid a memset for a memo they never
    // inserted a key into (clear_empty -- pure waste) vs one they did (clear_full).
    inline std::atomic<std::uint64_t> g_memo_max_buckets{0};
    inline std::atomic<std::uint64_t> g_memo_clear_empty{0};
    inline std::atomic<std::uint64_t> g_memo_clear_full{0};
    inline std::atomic<std::uint64_t> g_memo_reset{0};      // over-cap tables swapped out for a fresh one
    struct Dumper {
        ~Dumper()
        {
            if (!Enabled()) { return; }
            const unsigned long long top = g_backtrack_entries.load();
            const unsigned long long nodes = g_nodes.load();
            const unsigned long long eok = g_entries_ok.load(), efail = g_entries_fail.load();
            const unsigned long long nok = g_nodes_ok.load(), nfail = g_nodes_fail.load();
            std::fprintf(stderr,
                "\n=== TAP STATS: top-level entries=%llu  total nodes=%llu  nodes/entry=%.1f"
                "  memo-off top-level=%llu  max board n=%llu  max sources=%llu ===\n",
                top, nodes, top ? (double)nodes / (double)top : 0.0,
                (unsigned long long)g_top_memo_off.load(), (unsigned long long)g_max_n.load(),
                (unsigned long long)g_max_src.load());
            std::fprintf(stderr,
                "=== TAP OUTCOME: payable entries=%llu (%.1f%%, %llu nodes, %.1f/entry)  "
                "UNpayable entries=%llu (%.1f%%, %llu nodes = %.1f%% of nodes, %.1f/entry) ===\n",
                eok,   (eok + efail) ? 100.0 * (double)eok / (double)(eok + efail) : 0.0,
                nok,   eok ? (double)nok / (double)eok : 0.0,
                efail, (eok + efail) ? 100.0 * (double)efail / (double)(eok + efail) : 0.0,
                nfail, (nok + nfail) ? 100.0 * (double)nfail / (double)(nok + nfail) : 0.0,
                efail ? (double)nfail / (double)efail : 0.0);
            const unsigned long long fp = g_flow_prune.load(), fb = g_flow_bail.load();
            std::fprintf(stderr,
                "=== PAYMENT SITES: prepay-held=%llu  prepay-plain=%llu  per-cast=%llu  "
                "per-cast-filter=%llu  ||  BatchPrepay calls=%llu declined=%llu (%.1f%%) ===\n",
                (unsigned long long)g_site_prepay_held.load(),
                (unsigned long long)g_site_prepay_plain.load(),
                (unsigned long long)g_site_percast.load(),
                (unsigned long long)g_site_percast_filter.load(),
                (unsigned long long)g_prepay_calls.load(),
                (unsigned long long)g_prepay_declined.load(),
                g_prepay_calls.load() ? 100.0 * (double)g_prepay_declined.load()
                                              / (double)g_prepay_calls.load() : 0.0);
            const unsigned long long pacc = g_prepay_accepted.load();
            const unsigned long long potc = g_prepay_overtap_calls.load();
            std::fprintf(stderr,
                "=== PREPAY OVER-TAP: accepted=%llu  with-surplus=%llu (%.1f%%)  surplus-mana=%llu "
                "(%.2f per accepted)  excess-sources=%llu ===\n",
                pacc, potc, pacc ? 100.0 * (double)potc / (double)pacc : 0.0,
                (unsigned long long)g_prepay_overtap_mana.load(),
                pacc ? (double)g_prepay_overtap_mana.load() / (double)pacc : 0.0,
                (unsigned long long)g_prepay_overtap_srcs.load());
            const unsigned long long pshr = g_prepay_shrunk.load();
            std::fprintf(stderr,
                "=== PREPAY SHRINK: solves that gave a source back=%llu (%.1f%% of over-tapping)  "
                "sources returned=%llu ===\n",
                pshr, potc ? 100.0 * (double)pshr / (double)potc : 0.0,
                (unsigned long long)g_prepay_shrunk_srcs.load());
            std::fprintf(stderr,
                "=== FLOW PRUNE: pruned=%llu (%.1f%% of top-level entries)  bailed=%llu (%.1f%%) ===\n",
                fp, top ? 100.0 * (double)fp / (double)top : 0.0,
                fb, top ? 100.0 * (double)fb / (double)top : 0.0);
            std::fprintf(stderr,
                "=== DUP COLLAPSE: identical-sibling skips=%llu (%.2f per node) ===\n",
                (unsigned long long)g_dup_skips.load(),
                nodes ? (double)g_dup_skips.load() / (double)nodes : 0.0);
            const unsigned long long mh = g_mc_hit.load(), mm = g_mc_miss.load();
            const unsigned long long ms = g_mc_skip_shape.load(), mk = g_mc_skip_key.load();
            std::fprintf(stderr,
                "=== MANA CACHE: hit=%llu (%.1f%% of consulted)  miss=%llu  unstorable=%llu (%.1f%% of "
                "misses)  skipped: shape=%llu key=%llu (%.1f%% of all calls) ===\n",
                mh, (mh + mm) ? 100.0 * (double)mh / (double)(mh + mm) : 0.0, mm,
                (unsigned long long)g_mc_unstorable.load(),
                mm ? 100.0 * (double)g_mc_unstorable.load() / (double)mm : 0.0,
                ms, mk,
                (mh + mm + ms + mk) ? 100.0 * (double)(ms + mk) / (double)(mh + mm + ms + mk) : 0.0);
            const unsigned long long nmiss = g_mc_nodes_miss.load(), nskip = g_mc_nodes_skip.load();
            std::fprintf(stderr,
                "=== MANA CACHE NODES: miss=%llu (%.1f%% of nodes, %.1f/entry)  "
                "skipped-shape=%llu (%.1f%% of nodes, %.1f/entry) ===\n",
                nmiss, nodes ? 100.0 * (double)nmiss / (double)nodes : 0.0,
                mm ? (double)nmiss / (double)mm : 0.0,
                nskip, nodes ? 100.0 * (double)nskip / (double)nodes : 0.0,
                (ms + mk) ? (double)nskip / (double)(ms + mk) : 0.0);
            const unsigned long long ce = g_memo_clear_empty.load(), cf = g_memo_clear_full.load();
            std::fprintf(stderr,
                "=== MEMO BUCKETS: max retained=%llu (%.1f KB memset per clear at that size)  "
                "clears: empty=%llu (%.1f%% -- memset of an all-zero array) full=%llu  resets=%llu ===\n",
                (unsigned long long)g_memo_max_buckets.load(),
                (double)g_memo_max_buckets.load() * 8.0 / 1024.0,
                ce, (ce + cf) ? 100.0 * (double)ce / (double)(ce + cf) : 0.0, cf,
                (unsigned long long)g_memo_reset.load());
        }
    };
    inline Dumper g_dumper;
}

// Branch-and-bound gate for the backtracker (MTG_NO_MAXMANA_GATE disables it; A/B perf lever).
// Default ON: it is LOSSLESS (an upper bound only ever short-circuits provably-unpayable costs).
inline bool MaxManaGateEnabled()
{ static const bool v = !EnvOn("MTG_NO_MAXMANA_GATE"); return v; }

// FAILURE-MEMO INDEX SPACE. The memo's 64-bit key indexes the active player's tappable mana SOURCES,
// so the "won't fit in a bitmask" limit is a limit on the SOURCE COUNT -- but until 2026-08-16 it was
// tested against state.battlefield.size(), i.e. every permanent both players control. On a fanned-out
// board those differ enormously (Mirrorwing: n=105, sources=33, the other 63 being Treasure tokens),
// so the memo -- and, until it was decoupled, the flow-prune oracle with it -- was switched off by
// permanents it would never have indexed. Set to 1 to restore the old battlefield-indexed behaviour
// from ONE binary (A/B hatch; output must be byte-identical either way -- see the worker's key
// construction for why position-in-`cands` is a bijection onto the same equality classes).
// Namespace-scope inline const, not a function-local static: this is read per recursion node, where a
// thread-safe init guard measured ~0.9% self-cost (see the tapstats note above).
inline const bool g_tap_memo_bf_gate = EnvOn("MTG_TAP_MEMO_BF_GATE");
inline bool TapMemoBattlefieldGate() { return g_tap_memo_bf_gate; }

// Flow-prune oracle (byte-identical contention-aware infeasibility test at the top-level tap
// backtracker entry; see TapFlowInfeasible in SpellEffects.cpp). ON by default; MTG_NO_FLOW_PRUNE=1
// disables it (A/B off-switch -- output must be byte-identical either way).
inline bool FlowPruneEnabled()
{ static const bool v = !EnvOn("MTG_NO_FLOW_PRUNE"); return v; }

// FLOW-GUIDED TAP ORDER. The oracle above already computes a max-flow assignment for every FEASIBLE
// payment and discards it; this reuses it to order the backtracker's source loop, so the first
// branch tried is the one the flow already proved sufficient. Measured on FiveColour: the flow's
// source set is EXACTLY the set the backtracker ends up tapping 75.8% of the time, a superset a
// further 0.7%, overlapping 23.6%, and DISJOINT 0% -- it is never a wrong guess. Meanwhile a
// successful payment costs 71.3 nodes to rediscover an answer that averages 4.5 sources.
//
// NOT byte-identical, unlike every other lever here: a different source ORDER finds a different
// (equally legal) tap set, which leaves different sources untapped and so changes later decisions.
// It is therefore a HEURISTIC change, measured by the suite on avg-win-turn as well as cost, not a
// lossless one -- hence its own flag rather than folding into FlowPruneEnabled.
//
// STAYS DEFAULT OFF -- adoption was attempted on 2026-08-16 and REFUSED BY THE HELD-OUT SEEDS.
// Net avg-win-turn vs baseline (negative = better), summed over every case in each tier:
//
//                                smoke (s1001)   regression (s2002/3003)     TOTAL
//   flow order only                   +0.0082                   +0.0330    +0.0412
//   flow order + scarcity bias        -0.0550                   +0.0690    +0.0140
//
// Both are WORSE. The scarcity arm's -0.0550 on smoke is exactly the selection artifact the
// heuristic-optimization skill warns about -- it reverses on the held-out seeds, and only the SUM
// decides. With the cost saving measured at ~1% of runtime (section 8 of the design doc), there is
// nothing to trade the play regression against, so ground truth was deliberately NOT rebaselined.
//
// Kept behind the flag rather than deleted because the MECHANISM is sound and cheap to re-test: the
// oracle really does compute an assignment it currently discards, and the node counts really do fall
// 9.5-13.6x. What is unproven is that any particular assignment is a GOOD tap order. A future attempt
// should rank sources by something the engine already values, and must clear BOTH seed sets.
// MTG_FLOW_ORDER=1 enables it; MTG_FLOW_SCARCITY=0 then gives the bare (unbiased) order.
inline bool FlowOrderEnabled()
{ static const bool v = EnvOn("MTG_FLOW_ORDER"); return v; }

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

// Per-permanent bound: a storage land's max net = its live counter count (the whole burst); every
// other source falls back to the static SourceMaxNet(def). Used by the backtracker's B&B gate,
// which has the PERMANENT in scope -- a static bound would under-count a charged storage land and
// wrongly prune a payable cost (losslessness violation), so the counter count must be threaded in.
inline int SourceMaxNet(const Permanent& perm, const CardDefinition& def)
{
    if (def.params.storage_land) { return perm.storage_counters; }
    return SourceMaxNet(def);
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
        if (d->tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield))
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
        else if (d->params.storage_land && p.storage_counters > 0)
        {
            reserve = true;   // storage battery -> hold the burst; don't spend counters unless needed
        }
        if (reserve) { mask |= (1ull << i); }
    }
    return mask;
}

// TapForCostBacktrack -- body in SpellEffects.cpp (see the header note above).
// `src_cands` (threaded, nullptr at the top-level call) is the controller's structural mana sources
// {battlefield index, cached def}, enumerated ONCE at the top-level call and reused by every recursion
// node -- so a token-flooded OPPONENT board (Forbidden Orchard Spirits / Hunted Phantasm / Varchild's
// Survivors) is skipped instead of rescanned (with a fresh LookupCached hash lookup) at every one of the
// millions of backtrack nodes. Invariant during a payment (no permanent enters/leaves), so this is a
// pure byte-identical speedup: every per-node liveness check (tapped / CanTapNow / storage / graveyard
// fuel / reservation) still runs, only the O(battlefield) filter + LookupCached are hoisted.
bool TapForCostBacktrack(GameState& state, const ManaCost& cost,
                                bool for_creature, ManaPool floating,
                                const std::vector<Color>* rp_colors = nullptr,
                                TapBacktrackMemo* fail_memo = nullptr,
                                ManaPool* out_leftover = nullptr,
                                std::uint64_t tapped_mask = 0,
                                int untapped_max = -1,
                                std::uint64_t reserved_mask = 0,
                                ManaPool* out_full_pool = nullptr,
                                const std::vector<std::pair<int, const CardDefinition*>>* src_cands = nullptr);

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
