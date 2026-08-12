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
#include "../cards/CardDatabase.h"
#include "../ai/DecisionProviders.h"   // ResolveProvider: route deck decisions through the provider
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
    std::vector<char> taken(static_cast<std::size_t>(n), 0);
    auto push = [&](int i)
    { if (i >= 0 && i < n && !taken[static_cast<std::size_t>(i)]) { taken[static_cast<std::size_t>(i)] = 1; out.push_back(i); } };

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
            if (CleanupDiscardProtected(state, ap.hand[i], required_pieces)) { continue; }
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
                if (CleanupDiscardProtected(state, ap.hand[i], required_pieces)) { continue; }
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
    std::vector<int> tier_b;
    for (int i = 0; i < n; ++i)
    {
        if (ap.hand[i].m_is_staged) { continue; }
        if (CleanupDiscardProtected(state, ap.hand[i], required_pieces)) { continue; }
        tier_b.push_back(i);
    }
    std::stable_sort(tier_b.begin(), tier_b.end(), [&](int a, int b)
    { return CleanupDiscardManaValue(ap.hand[a]) > CleanupDiscardManaValue(ap.hand[b]); });
    for (int i : tier_b) { push(i); }

    // Tier C -- last resort: non-staged before staged, then max mana value, stable.
    std::vector<int> tier_c;
    for (int i = 0; i < n; ++i) { if (!taken[static_cast<std::size_t>(i)]) { tier_c.push_back(i); } }
    std::stable_sort(tier_c.begin(), tier_c.end(), [&](int a, int b)
    {
        const bool sa = ap.hand[a].m_is_staged, sb = ap.hand[b].m_is_staged;
        if (sa != sb) { return !sa; }
        return CleanupDiscardManaValue(ap.hand[a]) > CleanupDiscardManaValue(ap.hand[b]);
    });
    for (int i : tier_c) { push(i); }

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
        TRACE("discard", "T%d hand=%zu cands=%zu lip=%d landsinhand=%d tower=%d dropopen=%d -> %s",
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

// Forward declaration: CreateToken is defined further down but used by FireOnCastTriggers.
inline void CreateToken(GameState&, int, int, int, const std::vector<std::string>&);
// Forward declaration: the Dragonstorm-engine cascade (Scourge ping + Lathliss token) is
// mutually recursive with CreateToken (a Lathliss 5/5 token entering re-fires the cascade).
// Defined after CreateToken; called from CreateToken so EVERY token dragon enter also pings.
inline void OnDragonEnters(GameState&, int controller, int entered_index);
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
// OnDragonEnters cascade (Scourge ping / Lathliss token) so a put Dragon is a live body, not inert
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
inline void PerformTutorToBattlefield(GameState& state, int controller, const CardParams& pp,
                                      int max_puts,
                                      const std::vector<std::string>& preferred = {},
                                      const std::string& source_name = {})
{
    if (max_puts <= 0 || pp.tutor_types.empty()) { return; }
    Player& ap = state.players[controller];

    auto matches_types = [&](const Card& c) -> bool {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        const Card& card = d ? d->card : c;
        for (const std::string& t : pp.tutor_types)
        { if (CardMatchesTypeName(card, t)) { return true; } }
        return false;
    };

    // The put-list: the SEARCHED `preferred` if the caller supplied one, else the deck provider's
    // TutorToBattlefieldPutOrder heuristic (DragonstormProvider = Lathliss-first/Scourge-second
    // SELECTION + order; every non-Dragonstorm deck -- and Dragonstorm under MTG_UNPRUNED -- returns
    // {} -> byte-identical). This list is an EXACT ordered multiset (repeats == multiplicity).
    std::vector<std::string> put_pref = preferred;
    if (put_pref.empty())
    {
        put_pref = ResolveProvider(state).TutorToBattlefieldPutOrder(state, controller, pp, max_puts);
    }

    // Human-play override: let the player pick WHICH library Dragons enter (the engine keeps the rule's
    // play ORDER). Fires only on the REAL resolution -- g_play_dragon_chooser is nulled by RevealLogPause
    // for every search/rollout/enumeration scope, so the autonomous + search default is the rule ->
    // byte-identical. See GameLogger.h DragonChooser. `human_override` then SKIPS the TutorCandidates
    // fill pass below, so a human who deselects Dragons puts fewer than max_puts (never topped back up).
    bool human_override = false;
    if (g_play_dragon_chooser && max_puts > 0)
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
    // number), fire OnDragonEnters. Re-find per put -- erase shifts library indices, and OnDragonEnters
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
            EmitPlayEvent(state.turn_number, "dragonstorm",
                          "\xF0\x9F\x90\x89 " + lc.m_name.str()
                          + " -- put onto the battlefield (Dragonstorm)");
        }
        // #1 wiring requirement: route the put Dragon through the SAME cascade the hard-cast enter
        // uses (Scourge ping -> opponent life loss; Lathliss 5/5 token; token-first ordering baked in).
        OnDragonEnters(state, controller, static_cast<int>(state.battlefield.size()) - 1);
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

// True if `creature` deals combat damage with lifelink -- its own keyword or any attached
// aura_grants_lifelink Aura you control. Combat sites gain the controller that much life.
inline bool CreatureHasLifelink(const Permanent& creature, const GameState& state)
{
    if (creature.card.HasKeyword(Keyword::Lifelink)) { return true; }
    for (const Permanent& a : state.battlefield)
    {
        if (a.aura_attached_to != creature.card.m_number) { continue; }
        if (a.controller_index != creature.controller_index) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(a.card);
        if (d && d->params.is_aura && d->params.aura_grants_lifelink) { return true; }
    }
    return false;
}

// The m_numbers of creatures `controller` controls that an Aura with these params may legally
// enchant (CR 601.2c). Empty => the aura is uncastable (no legal target) and stays in hand.
inline std::vector<int> LegalEnchantTargets(const GameState& state, int controller,
                                            const CardParams& pp)
{
    std::vector<int> out;
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
inline int ResolveEnchantTarget(const GameState& state, int controller, int enchant_target)
{
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
                                     def->params.cast_token_subtypes});
            }
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
// controlled_lord_idx (optional): pre-filtered battlefield indices of the controller's LordEffect
// permanents. When a caller loops over many creatures against an UNCHANGING battlefield (combat,
// board eval), it can collect this list ONCE instead of having every creature re-scan the whole
// battlefield for the (usually zero) lords -- the per-creature scan was ~3% of a rollout on a
// lord-less deck. Byte-identical: the list is exactly the permanents the in-loop filter keeps
// (controller + LordEffect), and the per-creature logic below (self-exclusion by address, subtype
// matching, scales_per_matching count) is applied unchanged, so no lord is double-counted and an
// "other"-lord still skips itself. nullptr => scan the whole battlefield (original behaviour).
inline std::pair<int,int> ComputeLordBonus(
    const Card&                    creature,
    const std::vector<Permanent>&  battlefield,
    int                            controller_index,
    bool                           all_creature_types = false,
    const Permanent*               self               = nullptr,
    const std::vector<int>*        controlled_lord_idx = nullptr)
{
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
        if (!ldef || ldef->tmpl != CardTemplate::LordEffect) { return; }

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
    if (!p.entered_this_turn)                   { return true; }
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
// creature entering on EITHER side. Called from the universal enter cascade (OnDragonEnters' top,
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
            if (log) { note(w.controller_index, w.card.m_name.str()); }
        }
        // "Whenever another creature you control enters, you [may] gain N" (Suture Priest cl. 1).
        if (wp.own_creature_enters_lifegain > 0 && w.controller_index == entered_controller)
        {
            state.players[w.controller_index].life += wp.own_creature_enters_lifegain;
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
    token.card.m_number    = state.next_token_number++;   // unique per-copy id (GameState.h note)
    token.controller_index = controller_index;
    token.owner_index      = controller_index;
    token.entered_this_turn = true;
    token.is_token          = true;   // Lathliss "nontoken Dragon" gate reads this (loop-safe)
    state.battlefield.push_back(token);
    // A token Dragon (Lathliss 5/5, Utvara 6/6) entering also fires the Dragonstorm cascade: it
    // re-pings every Scourge (via OnDragonEnters step 2) but, being a token, never re-triggers
    // Lathliss (nontoken gate). No-op for every non-Dragon token (early subtype return) -> all
    // existing token-making decks (Adeline, Forbidden Orchard, Sliver Hive) are byte-identical.
    OnDragonEnters(state, controller_index, static_cast<int>(state.battlefield.size()) - 1);
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

// A Dragon just entered under `controller` at battlefield slot `entered_index`. Drives BOTH
// Scourge of Valkas ("this or another Dragon enters -> deal X = Dragons you control to any
// target", modelled as opponent life loss) and Lathliss ("another NONTOKEN Dragon enters ->
// 5/5 Dragon token"). Deterministic token-first ordering (the unambiguously optimal goldfish
// line): create Lathliss's token FIRST (it re-pings Scourge via the recursive CreateToken at the
// new, higher count), THEN resolve the newcomer's own Scourge pings at that higher count.
//
// Loop-safety: a Scourge ping creates no permanents (can't loop); a Lathliss token is is_token so
// step 1 skips it (never re-triggers Lathliss) though it still re-pings Scourge (correct).
inline void OnDragonEnters(GameState& state, int controller, int entered_index)
{
    if (entered_index < 0 || entered_index >= static_cast<int>(state.battlefield.size())) { return; }
    // Creature-enter watchers (Creature Giving: Wardens / Suture Priest). This function is the
    // UNIVERSAL enter cascade -- every enter site already calls it -- so the watcher fire lives at
    // its top, BEFORE the Dragon-subtype early-out, gated on the newcomer being a creature. A gift
    // token created for the OPPONENT (CreateToken -> here) therefore drains through Suture Priest
    // exactly like a cast creature. Param-gated scan -> byte-identical for every other deck.
    if (state.battlefield[entered_index].card.IsCreature())
    { FireCreatureEnterWatchers(state, state.battlefield[entered_index].controller_index, entered_index); }
    // Early out for every non-Dragon enter (keeps all other token/creature decks byte-identical).
    if (!CardHasSubtype(state.battlefield[entered_index].card, "Dragon")) { return; }
    const bool entered_is_token = state.battlefield[entered_index].is_token;
    // Copy the newcomer's subtypes: CreateToken below push_backs and may reallocate the vector,
    // which would dangle a reference (entered_index itself stays valid -- tokens only append).
    const Card entered_card = state.battlefield[entered_index].card;

    // STEP 1 -- Lathliss tokens (created first). Each Lathliss makes a 5/5 for the newcomer if it
    // is a NONTOKEN Dragon that is not the Lathliss itself and matches the required subtype.
    if (!entered_is_token)
    {
        struct Spec { int p, t; std::vector<std::string> subs; };
        std::vector<Spec> specs;
        const int bf_size = static_cast<int>(state.battlefield.size());
        for (int i = 0; i < bf_size; ++i)
        {
            if (i == entered_index) { continue; }                 // "another Dragon" -- not the source
            const Permanent& src = state.battlefield[i];
            if (src.controller_index != controller) { continue; }
            const CardDefinition* sdef = CardDatabase::Instance().LookupCached(src.card);
            if (!sdef || !sdef->params.etb_other_subtype_creates_tokens) { continue; }
            if (!sdef->params.etb_token_requires_subtype.empty()
                && !CardHasSubtype(entered_card, sdef->params.etb_token_requires_subtype)) { continue; }
            specs.push_back({ sdef->params.etb_created_token_power,
                              sdef->params.etb_created_token_toughness,
                              sdef->params.etb_created_token_subtypes });
        }
        for (const Spec& s : specs)
        {
            CreateToken(state, controller, s.p, s.t, s.subs);      // recursively re-pings Scourge
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
inline void OnGoblinEnters(GameState& state, int controller, int entered_index,
                           const std::string& chosen_tutor = "")
{
    if (entered_index < 0 || entered_index >= static_cast<int>(state.battlefield.size())) { return; }
    const CardDefinition* def =
        CardDatabase::Instance().LookupCached(state.battlefield[entered_index].card);
    if (!def) { return; }
    const CardParams& p = def->params;

    // (1) ETB fixed-token creation ("create N 1/1 red Goblin tokens" -- Mogg War Marshal 1,
    //     Siege-Gang Commander 3). Uses the shared etb_created_token_* spec.
    for (int k = 0; k < p.etb_self_creates_tokens; ++k)
    {
        CreateToken(state, controller, p.etb_created_token_power,
                    p.etb_created_token_toughness, p.etb_created_token_subtypes);
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
    const int face_dmg = p.etb_damage_any + p.etb_damage_each_opponent;
    if (face_dmg > 0)
    {
        state.players[opp].life -= face_dmg;
        state.opponent_lost_life_this_turn = true;
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
inline void OnCreatureDies(GameState& state, int dead_controller, const Card& dead_card)
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
                        wp.dies_token_toughness, wp.dies_token_subtypes);
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
        OnDragonEnters(state, controller, static_cast<int>(state.battlefield.size()) - 1);
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
inline void ApplyLoyaltyAbility(GameState& state, int controller, int walker_id, int ability_index)
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
        // Bolas +3 ("destroy target noncreature permanent"): vs a permanent-less passive opponent
        // the only legal targets are OUR OWN noncreature permanents -- a real, costly loyalty
        // ramp the search prices holistically. AUTO-RESOLVED pick (disclosed): the first own
        // LAND in battlefield order (most replaceable), else the first own noncreature
        // non-planeswalker permanent, excluding the activating walker itself.
        int pick = -1;
        for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
        {
            const Permanent& q = state.battlefield[i];
            if (q.controller_index != controller || q.card.IsCreature()) { continue; }
            if (q.card.m_number == walker_id) { continue; }
            if (q.card.IsLand()) { pick = i; break; }
            if (pick < 0 && !q.card.HasType(CardType::Planeswalker)) { pick = i; }
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
        // Oko +1 ("target artifact or creature loses all abilities, becomes a 3/3 green Elk"):
        // NARROWED to the value line -- transform an own Food token into a 3/3 body (enumerated
        // only when a Food exists; Elking a real creature is strictly worse here and is not
        // offered -- disclosed 6a). Keeps entered_this_turn (a fresh Food makes a summoning-sick
        // Elk, faithful).
        for (Permanent& q : state.battlefield)
        {
            if (q.controller_index != controller) { continue; }
            if (q.card.m_name.str() != "Food Token") { continue; }
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
            break;
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
inline void ApplyEquip(GameState& state, int controller, int equip_id, int creature_id)
{
    Permanent* eq = nullptr;
    bool host_ok = false;
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index != controller) { continue; }
        if (p.card.m_number == equip_id)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (d && d->params.is_equipment) { eq = &p; }
        }
        if (p.card.m_number == creature_id && (p.card.IsCreature() || p.is_animated))
        { host_ok = true; }
    }
    if (eq && host_ok) { eq->equipped_to = creature_id; }
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
        }
        return;
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
inline int CanonicalSacVictim(const GameState& state, int controller, int source_id,
                              const std::string& need_sub)
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
        if (v.controller_index != controller || !v.card.IsCreature()) { continue; }
        if (!need_sub.empty() && !CardHasSubtype(v.card, need_sub)) { continue; }
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
    const Card dead = state.battlefield[idx].card;
    state.players[controller].graveyard.push_back(dead);
    state.battlefield.erase(state.battlefield.begin() + idx);
    OnCreatureDies(state, controller, dead);
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
    Card victim; bool found = false;
    if (hidx >= 0)
    {
        victim = state.battlefield[hidx].card; found = true;
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
            if (q.controller_index != controller || !q.card.IsCreature()) { continue; }
            if (q.card.m_number != victim_id) { continue; }
            if (!op->sac_creature_requires_subtype.empty()
                && !CardHasSubtype(q.card, op->sac_creature_requires_subtype)) { continue; }
            victim = q.card; found = true;
            state.players[controller].graveyard.push_back(q.card);
            state.battlefield.erase(state.battlefield.begin() + i);
            break;
        }
        if (found || attempt > 0 || !victim_was_token) { break; }
        victim_id = CanonicalSacVictim(state, controller, source_id,
                                       op->sac_creature_requires_subtype);
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
    OnCreatureDies(state, controller, victim);   // Pashalik ping / Rundvelt impulse / Mogg death token
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
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index == controller && p.card.m_number == source_id)
        {
            const CardDefinition* od = CardDatabase::Instance().LookupCached(p.card);
            if (od) { need_sub = od->params.sac_creature_requires_subtype; }
            break;
        }
    }
    for (int n = 0; n < count; ++n)
    {
        int victim_id = CanonicalSacVictim(state, controller, source_id, need_sub);
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
// enter UNTAPPED + summoning-sick via CreateToken (so each fires OnDragonEnters: Scourge ping /
// Lathliss token) and are NOT returned to this combat. Counts are gathered BEFORE any CreateToken
// (which invalidates the `attackers` pointers). Distinct from the flat tapped-and-attacking
// FireAttackCreateTokens (Adeline).
inline void FireUtvaraAttackTokens(GameState& state, int controller,
                                   const std::vector<const Permanent*>& attackers)
{
    if (attackers.empty()) { return; }
    struct Spec { int n, p, t; std::vector<std::string> subs; };
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
                          sdef->params.attack_per_token_subtypes });
    }
    for (const Spec& s : specs)
    {
        for (int k = 0; k < s.n; ++k)
        {
            CreateToken(state, controller, s.p, s.t, s.subs);   // untapped; pings via OnDragonEnters
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
// (OnDragonEnters + OnGoblinEnters -- mirroring PerformMuxusReveal), so the cheated body fires its own
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
            EmitReveal(state.turn_number, src.card.m_name.str() + " (put)",
                       { raw.m_number }, { raw.m_name.str() }, { raw.m_number }, {},
                       /*dispositions*/ { "\xE2\x86\x92 battlefield (from hand)" });
        }
        OnDragonEnters(state, controller, slot);   // in case a put permanent is ever a Dragon
        OnGoblinEnters(state, controller, slot);   // fire the put permanent's own Goblin ETB cascade
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
// enters as its permanent; a Dragon fires OnDragonEnters (Lotus Bloom is a colourless artifact -> no-op).
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
    OnDragonEnters(state, controller, static_cast<int>(state.battlefield.size()) - 1);
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
                for (int n = 0; n < count; ++n)
                {
                    const int vid = CanonicalSacVictim(state, controller, sac_source_id, need_sub);
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
    // Snapshot then clear so a re-entrant OnDragonEnters (a suspended Dragon would ping/spawn) cannot
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
        ++p.age_counters;
        for (int k = 0; k < p.age_counters; ++k)
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
            OnDragonEnters(state, active, slot);   // universal cascade (enter-watchers fire here)
            OnGoblinEnters(state, active, slot);   // param-gated ETBs: Phantasm gift / Wurm sweep
        }

        // "...then shuffle" (deterministic + lockstep; inert unless MTG_SEARCH_SHUFFLE).
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
// (OnDragonEnters top fires the creature-enter watchers; OnGoblinEnters fires a copied Goblin
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
    OnDragonEnters(state, controller, idx);
    OnGoblinEnters(state, controller, idx);
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
inline void ApplyTrickPayload(GameState& state, int controller, const CardDefinition& def, int ti)
{
    const CardParams& pp = def.params;
    Player& pl = state.players[controller];

    // Untargeted riders (resolve for every instance, targeted or not).
    TrickDraw(state, controller, pp.cast_draw);
    if (pp.creates_treasures > 0) { CreateTreasureTokens(state, controller, pp.creates_treasures); }
    if (pp.cast_lifegain > 0)     { pl.life += pp.cast_lifegain; }
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
    if (pw != 0) { tgt.temp_power_bonus += pw; }
    if (tf != 0) { tgt.temp_tough_bonus += tf; }

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
                                   int target_number, int strive_extras)
{
    // Resolve the declared target (by stable per-copy id; deck cards AND tokens are numbered --
    // see GameState::next_token_number).
    int ti = -1;
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

    for (int i : order) { ApplyTrickPayload(state, controller, def, i); }
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
inline int PermanentManaYield(const Permanent& perm, const CardDefinition& def)
{
    if (def.params.storage_land) { return perm.storage_counters; }
    return ManaProducedPerTap(def);
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
        total += PermanentManaYield(p, *d);
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
inline void AddSourceToPool(ManaPool& pool, const GameState& state, const CardDefinition& def,
                            int yield_override = -1);  // below
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
inline void AddChosenColorFloat(GameState& state, const std::string& col, int amt)
{
    if (amt <= 0) { return; }
    if (col.empty())      { state.floating_mana.wild      += amt; }
    else if (col == "W")  { state.floating_mana.white     += amt; }
    else if (col == "U")  { state.floating_mana.blue      += amt; }
    else if (col == "B")  { state.floating_mana.black     += amt; }
    else if (col == "R")  { state.floating_mana.red       += amt; }
    else if (col == "G")  { state.floating_mana.green     += amt; }
    else if (col == "C")  { state.floating_mana.colorless += amt; }
    else                  { state.floating_mana.wild      += amt; }  // unknown -> wild
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
inline int HinataGenericDiscount(const CardDefinition& def, const GameState& state, int chosen_x,
                                 int crackle_targets = -1)
{
    if (!HinataInPlay(state)) { return 0; }
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

// The colours a mana source currently produces. Identical to def.params.produces (by const ref,
// zero cost, byte-identical) for every normal source; the dynamic Reflecting-Pool union only for
// a `reflecting` source; the dynamic colour-domain union for a `domain_mana` source (Faeburrow /
// Bloom Tender). controller = the source's controller (active player on the battlefield).
inline const std::vector<Color>& EffectiveProduces(const GameState& state, int controller,
                                                   const CardDefinition& def, bool in_hand = false)
{
    if (def.params.domain_mana) { return DomainColors(state, controller); }
    if (!def.params.reflecting) { return def.params.produces; }
    return ReflectedColors(state, controller, in_hand);
}

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
    if (col != Color::Colorless && !for_creature)
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
                            int yield_override)
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
    // Three Tree City scaled ability: "{2},{T}: add N of a chosen colour", N = creatures you control
    // (any-creature simplification). Modelled like a ramp filter but with a {feeder}-generic feed and
    // a board-count yield: when a feeder can pay the {feeder} and the net (N - feeder) beats the basic
    // {C} tap, this source contributes that net as WILD (the colour is search-chosen). Otherwise it
    // falls through to its basic "{T}: Add {C}" produces below. Empty subtype -> never taken here.
    if (IsScaledManaLand(def))
    {
        if (int net = ScaledManaNetYield(state, def)) { pool.wild += net; return; }
    }
    int amt = (yield_override >= 0) ? yield_override : ManaProducedPerTap(def);
    // Reflecting Pool: its colours are the union of the controller's other lands (empty -> adds
    // nothing, the solo-RP dead case). For every normal source this is the static produces[].
    const std::vector<Color>& prod = EffectiveProduces(state, state.active_player_index, def);
    // Domain source (Faeburrow / Bloom Tender): one mana of EACH colour among your permanents
    // from a single tap -- the yield is the dynamic colour count, ignoring the static amount.
    // Credited as wild like a Karoo (the one-of-each nuance is exact at tap time in tap_source).
    if (def.params.domain_mana) { amt = static_cast<int>(prod.size()); }
    if (prod.size() == 1)      { pool.Add(prod[0], amt); }
    else if (!prod.empty())    { pool.wild += amt; }
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
        const bool is_dork = (d->tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield)) || d->params.mana_rock;
        if (!is_land && !is_dork) { continue; }
        // Deathrite: credit at most #graveyard-lands such sources (mirrors AvailableManaPool).
        if (d->params.gy_land_exile_mana)
        {
            if (gy_fuel < 0) { gy_fuel = GraveyardLandFuel(state, controller); }
            if (gy_fuel <= 0) { continue; }
            --gy_fuel;
        }
        AddSourceToPool(pool, state, *d, PermanentManaYield(p, *d));
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

// The heuristic pick first, then every OTHER legal disposition in EnumerateTopDispositions order.
// Deduped against the heuristic by top_order + shuffle so candidate 0 is never repeated later.
inline std::vector<TopDisposition> TopDispositionCandidates(const GameState& state,
                                                            const std::vector<Card>& looked,
                                                            LookKind kind, int keep_decision)
{
    std::vector<TopDisposition> out;
    out.push_back(HeuristicTopDisposition(state, looked, kind, keep_decision));
    const TopDisposition& h = out.front();
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
        if (!ranked.empty()) { idx = ranked.front(); }
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
    }
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
    // "...then shuffle" (CR 701.19); deterministic + lockstep; inert unless MTG_SEARCH_SHUFFLE.
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
    // Namespace-scope (not function-local): a function-local `static const bool` forces a
    // thread-safe init-guard check on EVERY call, which the callgrind of a token-heavy rollout
    // showed as ~0.9% self-cost (called once per backtracker node, 39.8M x). An inline variable
    // is initialized once at static-init and read with no guard -- byte-identical value, zero
    // per-node overhead. Read only at runtime during payments, so no static-init-order hazard.
    inline const bool g_enabled = EnvOn("MTG_TAP_STATS");
    inline bool Enabled() { return g_enabled; }
    inline std::atomic<std::uint64_t> g_backtrack_entries{0};
    inline std::atomic<std::uint64_t> g_nodes{0};          // ALL recursion nodes (top-level + deep)
    inline std::atomic<std::uint64_t> g_top_memo_off{0};   // top-level calls with n>64 (fail-memo disabled)
    inline std::atomic<std::uint64_t> g_max_n{0};          // max battlefield size seen at a top-level call
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
                "  memo-off(n>64) top-level=%llu  max board n=%llu ===\n",
                top, nodes, top ? (double)nodes / (double)top : 0.0,
                (unsigned long long)g_top_memo_off.load(), (unsigned long long)g_max_n.load());
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
                "=== FLOW PRUNE: pruned=%llu (%.1f%% of top-level entries)  bailed=%llu (%.1f%%) ===\n",
                fp, top ? 100.0 * (double)fp / (double)top : 0.0,
                fb, top ? 100.0 * (double)fb / (double)top : 0.0);
        }
    };
    inline Dumper g_dumper;
}

// Branch-and-bound gate for the backtracker (MTG_NO_MAXMANA_GATE disables it; A/B perf lever).
// Default ON: it is LOSSLESS (an upper bound only ever short-circuits provably-unpayable costs).
inline bool MaxManaGateEnabled()
{ static const bool v = !EnvOn("MTG_NO_MAXMANA_GATE"); return v; }

// Flow-prune oracle (byte-identical contention-aware infeasibility test at the top-level tap
// backtracker entry; see TapFlowInfeasible in SpellEffects.cpp). ON by default; MTG_NO_FLOW_PRUNE=1
// disables it (A/B off-switch -- output must be byte-identical either way).
inline bool FlowPruneEnabled()
{ static const bool v = !EnvOn("MTG_NO_FLOW_PRUNE"); return v; }

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
