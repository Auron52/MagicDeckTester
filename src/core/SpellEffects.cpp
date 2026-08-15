// Out-of-line bodies for the largest COLD helpers declared in SpellEffects.h.
//
// WHY. SpellEffects.h is included by 15 translation units, so every `inline` body in it is parsed
// and code-generated in all of them. Measured on this machine at -O3: moving 1429 of the header's
// 5348 lines out cut TurnSolver.cpp 18.2s -> 16.0s (-12%) and AIEngine.cpp 7.2s -> 6.2s (-14%).
// That refines the backlog's "the header is not the dominant cost": it is not dominant, but it is
// a solid eighth of the critical-path TU.
//
// WHICH functions move is a real trade-off, because this build has NO LTO (no
// INTERPROCEDURAL_OPTIMIZATION anywhere in CMakeLists.txt): an out-of-line body can no longer be
// inlined into a caller in another TU. So the rule is:
//
//   MAY move  -- helpers that run once per RESOLUTION of a specific card or effect. The ones here
//                are a tutor resolution, a Light-Paws attach, a Muxus reveal, an ETB dig, an
//                Expressive Iteration resolution, a Karoo bounce, and the mana backtracker (an
//                exponential FALLBACK the scarcity-first tap order is designed to avoid entering).
//   MAY NOT   -- the per-cast / per-death / per-combat helpers: FireOnCastTriggers,
//                ApplyFirebreathing, OnCreatureDies, OnDragonEnters, TutorCandidates,
//                SelectCleanupDiscardIndex, FireCombatDamageCheatIntoPlay. Those fire on every
//                event of every rollout node, and this project is compute-bound.
//
// Verify any addition with a deterministic callgrind Ir A/B on a fixed workload. Do NOT reason
// about what looks hot: these helpers are fully inlined today, so they do not appear in a profile
// by name at all -- only the total Ir moves.
#include "SpellEffects.h"

// Execute a tutor (Idyllic / Enlightened): fetch `target_name` from the library and move it to
// hand (to_hand) or the top of the library (to_top). When target_name is empty, fall back to
// the heuristic's top candidate (TutorCandidates) -- so any path that doesn't carry a searched
// choice still plays the heuristic. The library is treated as already shuffled (the remaining
// order past the tutored card is a goldfish-irrelevant simplification that keeps the real game
// and rollout byte-consistent). Shared by EffectHandler (real) and ApplyPlanDirect (rollout).
void PerformTutor(GameState& state, int controller_index, const CardParams& pp,
                         const std::string& target_name,
                         const std::string& source_name)
{
    // Searched pick by INDEX (Plan::tutor_choice via ScriptedTutor, MTG_TUTOR_AXIS_RESOLVE=1).
    // Read-and-reset at entry so the FIRST tutor of the apply consumes it on every path (named
    // replay, whiff, scripted), mirroring g_scripted_etbdig_choice's consumed-once semantics.
    const int scripted_pick = g_scripted_tutor_choice;
    g_scripted_tutor_choice = -1;
    std::string want = target_name;
    if (want.empty())
    {
        std::vector<std::string> cands = ResolveProvider(state).TutorCandidates(state, controller_index, pp);
        if (cands.empty()) { return; }
        want = cands.front();
        if (scripted_pick >= 0)
        {
            // The ranking above ran on the TRUE resolution state -- the whole point of the index
            // axis. Dedup names in list order before indexing: fetching by name takes the first
            // matching library card, so three copies are ONE choice (same rule as the human chooser
            // below and MTG_TUTOR_CHOSEN_RANK). Clamped rather than dropped, because the enumerator
            // sized the axis off the turn-start state and the resolution-state list can be shorter
            // (the provider's cuts are state-dependent); clamping makes such a variant a duplicate
            // of the last candidate instead of a silent whiff.
            std::vector<std::string> uniq;
            for (const std::string& c : cands)
            { if (std::find(uniq.begin(), uniq.end(), c) == uniq.end()) { uniq.push_back(c); } }
            want = uniq[std::min<std::size_t>(static_cast<std::size_t>(scripted_pick),
                                              uniq.size() - 1)];
        }
        // Human play: a tutor resolving with NO searched target came from a PUT, not a cast (a Goblin
        // Lackey cheat, a Vial deploy, a Muxus reveal) -- there was no plan variant for the human to
        // pick in, so this used to search up cands.front() silently. Ask instead; -1 declines outright
        // ("you MAY search"). g_play_tutor_chooser is nulled by RevealLogPause for every
        // search/rollout/enumeration scope, so the search and every batch game keep taking the
        // heuristic front() and stay byte-identical. See GameLogger.h TutorChooser.
        if (g_play_tutor_chooser)
        {
            // Offer each NAME once: TutorCandidates lists library order, so a deck with three copies
            // of a card would otherwise show it three times, and picking any of them fetches the same
            // first matching library card anyway. Dedup preserves first-occurrence order.
            std::vector<std::string> uniq;
            for (const std::string& c : cands)
            { if (std::find(uniq.begin(), uniq.end(), c) == uniq.end()) { uniq.push_back(c); } }
            // The badged "AI pick" must be the provider's RANKED pick, not uniq[0] (viewer issue #7).
            // --claude-play forces MTG_UNPRUNED, and an archetype TutorCandidates returns the UNRANKED
            // GenericProvider list under that gate -- so `cands` here is raw LIBRARY ORDER and the old
            // hard-coded default of 0 badged whatever happened to be first (Goblins s22: Stingscourger,
            // over Krenko / Muxus / every lord). Re-ask the provider inside a HumanPlaySuppress scope,
            // which is exactly the guard that makes DecisionUnpruned() false, to get the real ranking.
            //
            // Only the DEFAULT INDEX changes -- the offered list keeps library order. Reordering it
            // would silently re-point every `tutor_etb` index already recorded under references/
            // (s22 recorded 11 = Goblin Chieftain), so the human still sees the same grid in the same
            // places and only the badge moves. Falls back to 0 if the ranked pick is somehow absent.
            int def = 0;
            {
                HumanPlaySuppress pruned;   // ranked (pruned) view; restores on scope exit
                const std::vector<std::string> ranked =
                    ResolveProvider(state).TutorCandidates(state, controller_index, pp);
                if (!ranked.empty())
                {
                    auto it = std::find(uniq.begin(), uniq.end(), ranked.front());
                    if (it != uniq.end()) { def = static_cast<int>(it - uniq.begin()); }
                }
            }
            const int picked = (*g_play_tutor_chooser)(state, controller_index, source_name, uniq, def);
            if (picked < 0) { return; }                                   // declined the optional search
            if (picked < static_cast<int>(uniq.size())) { want = uniq[picked]; }
        }
    }
    // DIAGNOSTIC (MTG_TUTOR_CHOSEN_RANK, default off): where in the ranking did the SEARCH actually
    // land? Gated on g_real_resolution, so it reports only the target the engine commits to, never
    // the thousands of hypothetical tutors inside rollouts.
    //
    // This exists because the obvious instrument does not work. Forcing the tutor to rank k
    // (MTG_TUTOR_FORCE_RANK) and reading the resulting win turn CANNOT distinguish "the ranking put
    // the right card out of the window" from "restricting the axis changed which turn Matron is cast
    // in". Goblins s7007 gi371 is the proof: forced rank 6 and forced rank 10 both fetch a Goblin
    // Piledriver, but rank 6 casts Matron on T4 and rank 10 casts it on T3 -- different decisions,
    // different board states, so the rank labels refer to candidate lists that never coexist. Any
    // per-rank table built that way mislabels plan changes as ranking misses.
    //
    // Reading the committed choice has no such confound: run wide (MTG_TUTOR_WIDTH=12) and see which
    // rank the search commits to. Ranks consistently past the shipped width are a real ranking miss
    // and the card names are trustworthy; ranks inside it mean the extra width bought plan diversity,
    // not a better fetch, and no reordering will recover those games.
    //
    // Ranked over NAMES deduped in list order, because fetching by name always takes the first
    // matching library card -- three copies of Piledriver are one choice, not three, and counting
    // them separately would inflate every rank below them.
    if (g_real_resolution)
    {
        static const bool s_chosen_rank = EnvOn("MTG_TUTOR_CHOSEN_RANK");
        if (s_chosen_rank)
        {
            std::vector<std::string> cands = ResolveProvider(state).TutorCandidates(state, controller_index, pp);
            std::vector<std::string> uniq;
            for (const std::string& c : cands)
            { if (std::find(uniq.begin(), uniq.end(), c) == uniq.end()) { uniq.push_back(c); } }
            int rank = -1;
            for (int i = 0; i < static_cast<int>(uniq.size()); ++i)
            { if (uniq[i] == want) { rank = i + 1; break; } }
            std::string top;
            for (int i = 0; i < static_cast<int>(uniq.size()) && i < 8; ++i)
            { top += (i ? " | " : "") + uniq[i]; }
            std::fprintf(stderr, "[tutor-chosen] T%d src=%s chose=%s rank=%d/%d :: %s\n",
                         state.turn_number, source_name.c_str(), want.c_str(),
                         rank, static_cast<int>(uniq.size()), top.c_str());
        }
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
    if (RevealVisible())
    {
        // Disposition must match the tutor's actual placement: Enlightened/Idyllic Tutor put the
        // card on TOP of the library, not in hand (2026-08-06 claude-play sweep flag, seed 9005).
        EmitReveal(state.turn_number, source_name + " (searched)",
                   { fetched_num }, { fetched_name }, { fetched_num }, {},
                   /*dispositions*/ { pp.tutor_to_top ? "to top" : "to hand" });
    }

    // Gamble: "then discard a card at random." Deterministic so the rollout and the real executor
    // pick the IDENTICAL victim -- the search resolves it clairvoyantly (the engine-wide
    // known-library simplification), but it can still hit the just-tutored card (the real Gamble
    // risk on a small hand). Only fires for to-hand tutors that set the flag; off everywhere else.
    //
    // KNOWN ORDER-SENSITIVITY, opt-in fix behind MTG_CANON_TUTOR_DISCARD (default OFF -- read this
    // before flipping it). The victim is an index into the hand AS STORED, and storage order is NOT
    // part of the rollout/executor lockstep contract: the rollout pushes staged (Soulfire / Expressive
    // Iteration / impulse) cards straight into hand while the executor merges Player::staged_cards at
    // its own breakpoints, so the two legitimately hold the SAME cards in a different order and then
    // shed DIFFERENT cards (Hinata seed 4010 post-fix: rollout Mountain, executor Island, same index).
    // MTG_CANON_TUTOR_DISCARD draws over ascending per-copy m_number instead -- still a uniform draw
    // over the same set, so the modelled randomness is unchanged; it just stops depending on a vector's
    // layout, and it does put the two sides in lockstep on the traced game.
    //
    // Why it is NOT the default (measured 2026-07-29). Suite A/B, all three modes: searched-depth net
    // -0.105, which splits into train (smoke+regression) -0.113 and HELD-OUT (overnight) +0.008 --
    // train-positive, held-out NEUTRAL. The d0 cases move +0.044, which is noise (no lookahead, so
    // changing which card a random discard takes just reshuffles). So this buys no measurable quality;
    // it is correctness only, against a 12+ case Hinata GT rebaseline.
    //
    // Its one fd-diverge (0 -> 1, Hinata seed 4259) was ROOT-CAUSED and is NOT a defect here: with the
    // default discard that game has no T5 win at all, even at width 4; this change CREATES one, and
    // only the default breakpoint continuation width (W=2) fails to cash it (MTG_BP_SEARCH=4 realises
    // T5). Realised turns are T6 either way. See docs/design/rollout-executor-lockstep.md.
    //
    // ORDER OF FIXES MATTERS. Enabling this while the two hands still differed in CONTENT took Hinata
    // 2 -> 4 per 500: any index-based pick lands on a different card when the sets differ, however it
    // is canonicalised. Two upstream fixes had to land first -- the rollout counting staged cards
    // toward the 7-card cleanup limit (SimulateEndAndStartNextTurn) and the rollout not stamping a
    // played land's per-copy m_number (PlayLandByName), which made a Karoo bounce hand the rollout an
    // unnumbered Island. Those took it 3 -> 1. See docs/design/rollout-executor-lockstep.md.
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
        // Canonical draw order: hand indices sorted by m_number (stable on ties, so two copies that
        // somehow share a number still resolve deterministically). Opt-in only.
        static const bool s_canon_discard = EnvOn("MTG_CANON_TUTOR_DISCARD");
        int victim = static_cast<int>(mix % ap.hand.size());
        if (s_canon_discard)
        {
            std::vector<int> order(ap.hand.size());
            for (int i = 0; i < static_cast<int>(ap.hand.size()); ++i) { order[i] = i; }
            std::stable_sort(order.begin(), order.end(),
                             [&](int a, int b) { return ap.hand[a].m_number < ap.hand[b].m_number; });
            victim = order[victim];
        }
        const int         victim_num  = ap.hand[victim].m_number;
        const std::string victim_name = ap.hand[victim].m_name;
        // Discard goes to the graveyard (CR 701.8 / a discarded card is put into its owner's
        // graveyard) -- the prior code erased it from hand without rezoning, so the card silently
        // left the game and never showed up in the graveyard zone. Inert for the search on every
        // current deck (no Gamble deck reads graveyard contents -- no retrace/delve/escape/
        // threshold), so this only restores the correct zone + surfaces the card to the viewer.
        static const bool dtrace = EnvOn("MTG_DISCARD_TRACE");
        if (dtrace)
        {
            std::string hs;
            for (const Card& hc : ap.hand)
            { hs += hc.m_name.str(); hs += "#"; hs += std::to_string(hc.m_number); hs += ","; }
            std::fprintf(stderr,
                         "[discard] T%d seed=%llu search_count=%llu handsize=%zu victim=%d(%s) hand=[%s]\n",
                         state.turn_number, static_cast<unsigned long long>(state.game_seed),
                         static_cast<unsigned long long>(state.search_count), ap.hand.size(),
                         victim, victim_name.c_str(), hs.c_str());
        }
        ap.graveyard.push_back(ap.hand[victim]);
        ap.hand.erase(ap.hand.begin() + victim);
        if (g_reveal_logger) { g_reveal_logger->LogDiscard(victim_num, victim_name); }
    }
}

// Light-Paws, Emperor's Voice: an Aura you CAST just resolved (mv = its mana value). For each
// Light-Paws you control, search your library for an Aura with mana value <= mv and a name different
// from every Aura you control (and whose own enchant restriction Light-Paws satisfies), put it onto
// the battlefield attached to that Light-Paws, then shuffle. WHICH aura is a heuristic pick (highest
// power contribution; disclosed 6a). The put does NOT re-trigger (it was not cast) -> bounded to one
// fetch per cast aura. Deterministic + lockstep (executor + rollout call it identically).
void PerformLightPawsAttach(GameState& state, int controller, int cast_aura_mv,
                                   const char* side)
{
    Player& ap = state.players[controller];
    // Light-Paws fetch/shuffle trace (MTG_LP_TRACE; DIAGNOSIS ONLY). `side` distinguishes the
    // rollout's committed-line replay (APPLY) from the real executor (EXEC) so the two fetch
    // sequences can be diffed -- how the legend-rule divergence below was found. Static: one getenv
    // for the process, so the off case is a predictable branch in this hot path.
    static const bool lp_trace = EnvOn("MTG_LP_TRACE");
    if (lp_trace)
    {
        int nlp = 0; std::string lpnums;
        for (const Permanent& q : state.battlefield)
        {
            const CardDefinition* qd = CardDatabase::Instance().LookupCached(q.card);
            if (qd && qd->params.aura_cast_tutor_attach && q.controller_index == controller)
            { ++nlp; lpnums += " #" + std::to_string(q.card.m_number); }
        }
        std::fprintf(stderr, "[lp:%s] turn=%d ENTER mv=%d search_count=%llu libsize=%zu lightpaws=%d[%s] top=%s\n",
                     side, state.turn_number, cast_aura_mv,
                     static_cast<unsigned long long>(state.search_count), ap.library.size(),
                     nlp, lpnums.c_str(),
                     ap.library.empty() ? "-" : ap.library[0].m_name.str().c_str());
    }
    // Names of every Aura the controller currently controls (the "different name than each Aura you
    // control" restriction). Recomputed per Light-Paws (a prior fetch adds a name).
    for (int li = 0; li < static_cast<int>(state.battlefield.size()); ++li)
    {
        const Permanent lp = state.battlefield[li];   // copy: library edits below don't touch it, but
        const CardDefinition* lpd = CardDatabase::Instance().LookupCached(lp.card);
        if (!lpd || !lpd->params.aura_cast_tutor_attach) { continue; }
        if (lp.controller_index != controller) { continue; }

        std::unordered_set<std::string> controlled_aura_names;
        for (const Permanent& p : state.battlefield)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (d && d->params.is_aura && p.controller_index == controller)
            { controlled_aura_names.insert(p.card.m_name); }
        }
        // Whether Light-Paws satisfies a candidate aura's own enchant restriction right now.
        auto lp_now = [&]() -> const Permanent& {
            for (Permanent& q : state.battlefield)
                if (q.card.m_number == lp.card.m_number && q.controller_index == controller) { return q; }
            return state.battlefield[li];
        };
        // Eligibility of a library Aura as a Light-Paws fetch: an Aura with MV <= the cast Aura's, a
        // name not already among the Auras you control, and whose own enchant restriction Light-Paws
        // itself satisfies right now. Shared by the heuristic pick and the human-play chooser view so
        // the two can never disagree on which Auras are fetchable.
        auto eligible = [&](int i, const CardDefinition* d) -> bool {
            if (!d || !d->params.is_aura) { return false; }
            if (d->card.m_mana_cost.ManaValue() > cast_aura_mv) { return false; }
            if (controlled_aura_names.count(ap.library[i].m_name)) { return false; }
            if (d->params.aura_enchant_requires == "another_aura" && !CreatureHasAura(lp_now(), state)) { return false; }
            if (d->params.aura_enchant_requires == "modified"     && !CreatureIsModified(lp_now(), state)) { return false; }
            return true;
        };
        // WHICH Aura to fetch is provider-owned (LightPawsAuraCandidates). This is a TUTOR, and it
        // was the one tutor in the engine that did not route through a provider. The base rule --
        // rank by the power the Aura would REALIZE if attached now, ties to higher MV -- is the
        // historical pick, so this is byte-identical. (A scaling Aura grants per matching permanent,
        // so on a wide board its true contribution dwarfs a flat +N; the old static coefficient
        // undervalued exactly those payoff Auras. MTG_LEGACY_LIGHTPAWS_STATIC restores it.)
        std::vector<int> lp_legal;
        for (int i = 0; i < static_cast<int>(ap.library.size()); ++i)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(ap.library[i]);
            if (eligible(i, d)) { lp_legal.push_back(i); }
        }
        if (lp_legal.empty()) { continue; }   // no eligible aura -> no put
        const std::vector<int> lp_ranked = ResolveProvider(state).LightPawsAuraCandidates(
            state, controller, lp_now(), lp_legal);
        if (lp_ranked.empty()) { continue; }   // provider declined the "may search"
        int best_idx = lp_ranked.front();

        // Human play (--claude-play/viewer): let the player choose WHICH Aura Light-Paws fetches (or
        // decline -- it is a "may search"). Nulled by RevealLogPause for every search/rollout scope, so
        // this fires only for the REAL resolution and autonomous play is byte-identical (chooser null ->
        // the heuristic best_idx path is untouched, no allocation). Show the whole library Aura pool (a
        // search reveals the library) with the fetchable ones flagged legal; the reply is an index into
        // that pool, or -1 to fetch nothing. Following the AI default (heur_pool_idx) reproduces best_idx.
        if (g_play_lightpaws_chooser)
        {
            std::vector<Card> pool; std::vector<int> pool_lib_index, legal;
            int heur_pool_idx = -1;
            for (int i = 0; i < static_cast<int>(ap.library.size()); ++i)
            {
                const CardDefinition* d = CardDatabase::Instance().LookupCached(ap.library[i]);
                if (!d || !d->params.is_aura) { continue; }
                int pi = static_cast<int>(pool.size());
                pool.push_back(ap.library[i]);
                pool_lib_index.push_back(i);
                if (eligible(i, d)) { legal.push_back(pi); }
                if (i == best_idx) { heur_pool_idx = pi; }
            }
            int picked = (*g_play_lightpaws_chooser)(state, controller, lp.card.m_name.str(),
                                                     pool, legal, heur_pool_idx);
            bool ok = (picked == -1);
            for (int li : legal) { if (li == picked) { ok = true; break; } }
            if (ok && picked == -1) { continue; }           // human declined the optional search
            if (ok) { best_idx = pool_lib_index[picked]; }   // else out-of-range reply -> keep heuristic
        }

        Card fetched = ap.library[best_idx];
        if (lp_trace)
        {
            // `rank` replaces the old `contrib` print: the ranking now lives in the provider, so the
            // engine no longer holds the winning score -- the position in the ranked list is the
            // equivalent diagnostic (0 = the provider's top pick).
            std::fprintf(stderr, "[lp:%s] turn=%d FETCH %s (idx=%d, rank=0/%zu)\n",
                         side, state.turn_number, fetched.m_name.str().c_str(), best_idx,
                         lp_ranked.size());
        }
        ap.library.erase(ap.library.begin() + best_idx);
        Permanent perm;
        const CardDefinition* fd = CardDatabase::Instance().LookupCached(fetched);
        perm.card              = fd ? fd->card : fetched;
        perm.card.m_number     = fetched.m_number;
        perm.controller_index  = controller;
        perm.owner_index       = controller;
        perm.entered_this_turn = true;
        perm.aura_attached_to  = lp.card.m_number;   // attached to Light-Paws
        state.battlefield.push_back(perm);
        ShuffleAfterSearch(state, controller);
        if (lp_trace)
        {
            std::fprintf(stderr, "[lp:%s] turn=%d POST-SHUFFLE search_count=%llu top=%s;%s;%s\n",
                         side, state.turn_number,
                         static_cast<unsigned long long>(state.search_count),
                         ap.library.size() > 0 ? ap.library[0].m_name.str().c_str() : "-",
                         ap.library.size() > 1 ? ap.library[1].m_name.str().c_str() : "-",
                         ap.library.size() > 2 ? ap.library[2].m_name.str().c_str() : "-");
        }
    }
}

// Muxus, Goblin Grandee: "reveal the top six cards; put all Goblin creature cards with mana value
// 5 or less onto the battlefield and the rest on the bottom of your library in a random order."
// Deterministic reveal order (the printed "random" bottom order is unobservable in goldfishing --
// same accepted collapse as etb_dig). Each put creature enters through OnGoblinEnters, so a
// revealed Siege-Gang/Mogg fires its own ETB tokens (the cascade). Lockstep executor + rollout.
void PerformMuxusReveal(GameState& state, int controller, const CardParams& pp)
{
    Player& ap = state.players[controller];
    if (pp.etb_reveal_count <= 0 || ap.library.empty()) { return; }

    // Draw the top N off the library (removes them); DrawN caps at the library size.
    std::vector<Card> revealed;
    ap.library.DrawN(pp.etb_reveal_count, revealed);
    if (revealed.empty()) { return; }
    std::vector<int>         revealed_nums;
    std::vector<std::string> revealed_names;
    for (const Card& rc : revealed)
    { revealed_nums.push_back(rc.m_number); revealed_names.push_back(rc.m_name.str()); }

    auto matches_put = [&](const Card& raw) -> bool {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(raw);
        const Card& c = d ? d->card : raw;
        if (pp.etb_reveal_put_creatures_only && !c.IsCreature()) { return false; }
        if (pp.etb_reveal_put_max_mv > 0 && c.m_mana_cost.ManaValue() > pp.etb_reveal_put_max_mv)
        { return false; }
        for (const std::string& sub : pp.etb_reveal_put_subtypes)
        { if (CardHasSubtype(c, sub)) { return true; } }
        return pp.etb_reveal_put_subtypes.empty();
    };

    // Report WHAT MUXUS DID, not just what it saw: the old call passed empty kept/bottomed lists, so
    // the viewer history showed six revealed cards with no indication of which hit the battlefield and
    // which went to the bottom. The split is MANDATORY, not a choice -- the card reads "put ALL Goblin
    // creature cards with mana value 5 or less onto the battlefield and the rest on the bottom" -- so
    // this is pure reporting. Computed here, before the put loop mutates the battlefield, and gated on
    // g_reveal_logger (null in search/rollout), so play stays byte-identical.
    if (RevealVisible())
    {
        std::vector<int>         put_nums, bottom_nums;
        std::vector<std::string> labels;
        for (const Card& rc : revealed)
        {
            const bool put = matches_put(rc);
            (put ? put_nums : bottom_nums).push_back(rc.m_number);
            labels.push_back(put ? "\xE2\x86\x92 battlefield" : "\xE2\x86\x92 bottom of library");
        }
        EmitReveal(state.turn_number, "Muxus (reveal)", revealed_nums, revealed_names,
                   put_nums, bottom_nums, /*dispositions*/ labels);
    }

    // Put matching cards onto the battlefield (each fires its own ETB cascade); bottom the rest.
    for (const Card& raw : revealed)
    {
        if (matches_put(raw))
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(raw);
            Permanent perm;
            perm.card              = d ? d->card : raw;
            perm.card.m_number     = raw.m_number;
            perm.controller_index  = controller;
            perm.owner_index       = controller;
            perm.entered_this_turn = true;
            state.battlefield.push_back(perm);
            const int slot = static_cast<int>(state.battlefield.size()) - 1;
            OnDragonEnters(state, controller, slot);   // in case a put creature is ever a Dragon
            OnGoblinEnters(state, controller, slot);   // fire the put creature's own Goblin ETB
        }
        else
        {
            ap.library.push_back(raw);   // to the bottom of the library
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
bool PerformEtbDig(GameState& state, int controller_index,
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
    // WHICH match to take is provider-owned (EtbDigCandidates). The base rule is the historical
    // "first legal match in look order", so this is byte-identical; look order is library order, so
    // that rule is effectively an arbitrary pick among the matches and providers should override it.
    const std::vector<int> ranked =
        ResolveProvider(state).EtbDigCandidates(state, controller_index, examined, legal);
    int take = ranked.empty() ? -1 : ranked.front();

    // SEARCHED pick: a plan variant may pin an index into `ranked` (consumed by this dig). Clamped
    // rather than dropped, because the enumerator sizes the axis off the library as it stood at
    // enumeration time -- an earlier cantrip in the same plan can shift the top N, leaving a pinned
    // index past the end. Clamping makes that variant a duplicate of the last one (identical state
    // -> the search tie-breaks to the first, which is the heuristic) instead of a silent no-dig.
    if (g_scripted_etbdig_choice >= 0 && !ranked.empty())
    {
        const int k = g_scripted_etbdig_choice;
        take = ranked[std::min<std::size_t>(static_cast<std::size_t>(k), ranked.size() - 1)];
    }
    g_scripted_etbdig_choice = -1;   // consumed by this dig (see the header note)

    // MTG_ETBDIG_TRACE: DIAGNOSTIC (no play change). Sizes the decision -- how many legal matches the
    // dig actually chooses among. One match is forced (no decision to make); the arbitrary-pick
    // defect only bites when there are two or more, and the count is also the branching factor a
    // searched axis would cost.
    static const bool s_dig_trace = EnvOn("MTG_ETBDIG_TRACE");
    if (s_dig_trace)
    {
        std::fprintf(stderr, "[etbdig] turn=%d looked=%d legal=%d\n",
                     state.turn_number, static_cast<int>(examined.size()),
                     static_cast<int>(legal.size()));
    }

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

// Balan's attach-all (see the SpellEffects.h declaration note). Ids collected FIRST: ApplyEquip
// can erase (a Wargear re-host sacrifices its former host) and reallocate the battlefield.
void ApplyAttachAllEquipment(GameState& state, int controller, int balan_id)
{
    bool balan_ok = false;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index == controller && p.card.m_number == balan_id
            && (p.card.IsCreature() || p.is_animated)) { balan_ok = true; break; }
    }
    if (!balan_ok) { return; }
    std::vector<int> to_move;
    for (const Permanent& e : state.battlefield)
    {
        if (e.controller_index != controller || e.equipped_to == balan_id) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(e.card);
        if (d && d->params.is_equipment) { to_move.push_back(e.card.m_number); }
    }
    for (int id : to_move) { ApplyEquip(state, controller, id, balan_id); }
}

// Stoneforge Mystic's tap-put (see the SpellEffects.h declaration note).
bool ApplyPutFromHand(GameState& state, int controller, int source_id,
                      const std::string& put_name)
{
    Permanent* src = nullptr;
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index == controller && p.card.m_number == source_id) { src = &p; break; }
    }
    if (src == nullptr || src->tapped || !CanTapNow(*src, state.battlefield)) { return false; }
    Player& ap = state.players[controller];
    int hi = -1;
    for (int h = 0; h < static_cast<int>(ap.hand.size()); ++h)
    {
        if (!ap.hand[h].m_is_staged && ap.hand[h].m_name == put_name) { hi = h; break; }
    }
    if (hi < 0) { return false; }
    const std::string src_name = src->card.m_name.str();
    src->tapped = true;
    Card raw = ap.hand[hi];
    ap.hand.erase(ap.hand.begin() + hi);
    const CardDefinition* d = CardDatabase::Instance().LookupCached(raw);
    Permanent perm;
    perm.card              = d ? d->card : raw;
    perm.card.m_number     = raw.m_number;
    perm.controller_index  = controller;
    perm.owner_index       = controller;
    perm.entered_this_turn = true;
    state.battlefield.push_back(perm);   // may reallocate -- `src` is dead from here on
    const int slot = static_cast<int>(state.battlefield.size()) - 1;
    if (RevealVisible())
    {
        EmitReveal(state.turn_number, src_name + " (put)",
                   { raw.m_number }, { raw.m_name.str() }, { raw.m_number }, {},
                   /*dispositions*/ { "\xE2\x86\x92 battlefield (from hand)" });
    }
    OnDragonEnters(state, controller, slot);   // universal cascade: Puresteel draw fires here
    OnGoblinEnters(state, controller, slot);
    if (state.battlefield[slot].card.HasSupertype(Supertype::Legendary))
    { EnforceLegendRule(state, controller); }
    return true;
}

// Umezawa's Jitte non-combat modes (see the SpellEffects.h declaration note).
void ApplyJitteMode(GameState& state, int controller, int jitte_id, int mode, int target_id)
{
    Permanent* je = nullptr;
    for (Permanent& p : state.battlefield)
    {
        if (p.controller_index == controller && p.card.m_number == jitte_id) { je = &p; break; }
    }
    if (je == nullptr || je->charge_counters <= 0) { return; }
    const CardDefinition* d = CardDatabase::Instance().LookupCached(je->card);
    if (d == nullptr) { return; }
    if (mode == 1)
    {
        int ti = -1;
        for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
        {
            const Permanent& t = state.battlefield[i];
            if (t.card.m_number == target_id && (t.card.IsCreature() || t.is_animated))
            { ti = i; break; }
        }
        if (ti < 0) { return; }   // target left -> fizzle, counter kept
        je->charge_counters -= 1;
        Permanent& t = state.battlefield[ti];
        t.temp_power_bonus += d->params.charge_minus_power;   // signed: -1/-1 mode carries -1s
        t.temp_tough_bonus += d->params.charge_minus_tough;
        // Inline death check, shared by both worlds (the rollout runs no SBA loop between
        // main-phase actions): effective toughness + statics <= 0 -> the creature dies. The
        // realistic target is an opponent SPAWN token (plain P/T, no death triggers, tokens
        // cease -- CR 111.7); a nontoken card goes to its owner's graveyard.
        int tough = t.EffectiveToughness()
                  + ComputeLordBonus(t.card, state.battlefield, t.controller_index,
                                     t.is_animated, &t).second
                  + AuraBonusFor(t, state).second
                  + EquipBonusFor(t, state).second;
        if (tough <= 0)
        {
            const int dead_num = t.card.m_number;
            const bool is_token = t.is_token;
            if (!is_token)
            { state.players[t.owner_index].graveyard.push_back(t.card); }
            state.battlefield.erase(state.battlefield.begin() + ti);
            for (Permanent& e : state.battlefield)
            {
                if (e.equipped_to      == dead_num) { e.equipped_to = 0; }
                if (e.aura_attached_to == dead_num) { e.aura_attached_to = 0; }
            }
        }
    }
    else if (mode == 2)
    {
        je->charge_counters -= 1;
        state.players[controller].life += d->params.charge_lifegain;
    }
}

// Armored Skyhunter's attack-trigger dig-and-attach (see the SpellEffects.h declaration note).
// One trigger per attacking permanent with attack_dig_attach_count, resolved in battlefield
// order (approved collapse of the controller's trigger-order choice; each dig sees the library
// as the previous one left it). Snapshots the triggering card numbers first: the puts below
// push_back onto the battlefield, and stale indices are the classic reallocation bug.
void FireAttackDigAttach(GameState& state, int controller, const std::vector<int>& atk_idx)
{
    if (atk_idx.empty()) { return; }
    Player& ap = state.players[controller];
    std::vector<int> trigger_nums;
    for (int idx : atk_idx)
    {
        if (idx < 0 || idx >= static_cast<int>(state.battlefield.size())) { continue; }
        const Permanent& p = state.battlefield[idx];
        if (p.controller_index != controller) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d && d->params.attack_dig_attach_count > 0) { trigger_nums.push_back(p.card.m_number); }
    }
    for (int tn : trigger_nums)
    {
        const CardDefinition* sdef = nullptr;
        std::string src_name;
        for (const Permanent& p : state.battlefield)
        {
            if (p.controller_index == controller && p.card.m_number == tn)
            { sdef = CardDatabase::Instance().LookupCached(p.card); src_name = p.card.m_name.str(); break; }
        }
        if (sdef == nullptr) { continue; }   // source left the battlefield mid-resolution
        const int look = std::min(sdef->params.attack_dig_attach_count,
                                  static_cast<int>(ap.library.size()));
        if (look <= 0) { continue; }

        std::vector<Card> examined;
        for (int i = 0; i < look; ++i)
        {
            examined.push_back(ap.library.front());
            ap.library.erase(ap.library.begin());
        }
        // Legal puts: any Equipment; an Aura only if it has a legal enchant target RIGHT NOW (an
        // Aura enters attached, CR 303.4f -- no target means it cannot be put at all). The Aura
        // half is structurally dead in KittyEquipment (zero Auras) but stays faithful.
        std::vector<int> legal;
        for (int i = 0; i < static_cast<int>(examined.size()); ++i)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(examined[i]);
            if (d == nullptr) { continue; }
            if (d->params.is_equipment) { legal.push_back(i); continue; }
            if (d->params.is_aura
                && !LegalEnchantTargets(state, controller, d->params).empty())
            { legal.push_back(i); }
        }
        const std::vector<int> ranked = ResolveProvider(state).AttackDigPutCandidates(
            state, controller, examined, legal);
        int take = ranked.empty() ? -1 : ranked.front();
        // Human play: the put pick rides the existing DIG chooser shape (examined + legal
        // indices, -1 declines the "may"). Nulled by RevealLogPause during search/rollout.
        if (!legal.empty() && g_play_dig_chooser)
        {
            int picked = (*g_play_dig_chooser)(state, controller, src_name, examined, legal, take);
            bool ok = (picked == -1);
            for (int li : legal) { if (li == picked) { ok = true; break; } }
            take = ok ? picked : take;
        }
        if (take >= 0)
        {
            Card raw = examined[take];
            const CardDefinition* d = CardDatabase::Instance().LookupCached(raw);
            Permanent perm;
            perm.card              = d ? d->card : raw;
            perm.card.m_number     = raw.m_number;
            perm.controller_index  = controller;
            perm.owner_index       = controller;
            perm.entered_this_turn = true;
            state.battlefield.push_back(perm);
            const int slot = static_cast<int>(state.battlefield.size()) - 1;
            if (RevealVisible())
            {
                EmitReveal(state.turn_number, src_name + " (attack dig)",
                           { raw.m_number }, { raw.m_name.str() }, { raw.m_number }, {},
                           /*dispositions*/ { "\xE2\x86\x92 battlefield (from library)" });
            }
            if (d != nullptr && d->params.is_equipment)
            {
                // WHICH creature it attaches to: provider's value-greedy attacker pick (0 =
                // leave unattached). The human may attach to ANY controlled creature (the
                // oracle allows non-attackers) or decline, via the attach-host chooser.
                int host_num = ResolveProvider(state).AttackDigAttachHost(
                    state, controller, raw, atk_idx);
                if (g_play_attach_host_chooser)
                {
                    std::vector<int> host_idx;
                    int heur = -1;
                    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
                    {
                        const Permanent& h = state.battlefield[i];
                        if (h.controller_index != controller) { continue; }
                        if (!h.card.IsCreature() && !h.is_animated) { continue; }
                        if (d->params.equip_min_power > 0
                            && EquipGatePowerOf(h, state) < d->params.equip_min_power) { continue; }
                        host_idx.push_back(i);
                        if (h.card.m_number == host_num) { heur = static_cast<int>(host_idx.size()) - 1; }
                    }
                    int picked = (*g_play_attach_host_chooser)(state, controller,
                                                               raw.m_name.str(), host_idx, heur);
                    if (picked == -1) { host_num = 0; }
                    else if (picked >= 0 && picked < static_cast<int>(host_idx.size()))
                    { host_num = state.battlefield[host_idx[picked]].card.m_number; }
                }
                if (host_num > 0) { ApplyEquip(state, controller, raw.m_number, host_num); }
            }
            else if (d != nullptr && d->params.is_aura)
            {
                std::vector<int> tg = LegalEnchantTargets(state, controller, d->params);
                if (!tg.empty()) { state.battlefield[slot].aura_attached_to = tg.front(); }
            }
            OnDragonEnters(state, controller, slot);   // universal cascade: Puresteel draw fires here
            OnGoblinEnters(state, controller, slot);
            if (state.battlefield[slot].card.HasSupertype(Supertype::Legendary))
            { EnforceLegendRule(state, controller); }   // a second Shadowspear / Jitte
        }
        for (int i = 0; i < static_cast<int>(examined.size()); ++i)
        {
            if (i == take) { continue; }
            ap.library.push_back(std::move(examined[i]));   // rest to the bottom, examined order
        }
    }
}

// Expressive Iteration {U}{R}: "Look at the top three cards of your library. Put one into your hand,
// put one on the bottom of your library, and exile one. You may play the exiled card this turn."
// Model: look at the top 3, rank by ScryKeepOnTop (wanted first); the most-wanted goes to HAND
// (banked), the next is EXILED and STAGED playable THIS TURN ONLY (m_staged_expiry = turn_number),
// the least goes to the BOTTOM. Shared by the executor (ResolveDrawSpell) and the rollout -> lockstep.
// (NOT modelled as draw-2: the second card can only be played this turn, not banked -- the prior
// draw:2 + cast_scry:3 entry over-rated it.)
void ResolveExpressiveIteration(GameState& state)
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
        EmitReveal(state.turn_number, "Expressive Iteration", seen_nums, seen_names, kept_nums, bottom_nums);
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
void BounceKarooLand(GameState& state, int controller, int self_index)
{
    // Depletion-sack timing parity (fd-diverge root cause, Mirrorwing 2026-08-11): the EXECUTOR
    // sacrifices an emptied depletion land immediately (CheckStateBasedActions after every cast),
    // but the ROLLOUT's ApplyPlanDirect sweeps only at END of plan -- so a mid-plan karoo bounce
    // saw a dead-but-still-tapped Sandstone Needle in the rollout (and bounced it, keeping Forest)
    // while the real game had already sacked it (and bounced the untapped Forest, stranding the
    // recorded chain's last cast: realized_win = predicted+1). The rules side with the executor
    // (the "no counters -> sacrifice" state trigger fires at once), so sack HERE, before the
    // candidate scan, in BOTH worlds: a no-op for the executor (already swept) and for every deck
    // without depletion lands or without karoos -- byte-identical outside the diverging case.
    // NB self_index indexes the just-entered karoo (the LAST battlefield slot in every caller);
    // erasing depleted lands below it would shift it, so re-locate the karoo by its slot surviving
    // the sweep: depleted lands are strictly earlier entries, so the karoo's index shifts down by
    // the number erased before it.
    {
        const int before = static_cast<int>(state.battlefield.size());
        int erased_before_self = 0;
        for (int i = 0; i < before; ++i)
        {
            if (i >= self_index) { break; }
            const Permanent& p = state.battlefield[i];
            if (!p.card.IsLand()) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (d == nullptr || d->params.enters_tapped_with_depletion <= 0) { continue; }
            bool has_counters = false;
            for (const Counter& c : p.counters)
            { if (c.type == Counter::Type::Depletion && c.count > 0) { has_counters = true; break; } }
            if (!has_counters) { ++erased_before_self; }
        }
        SacrificeDepletedLands(state);
        self_index -= erased_before_self;
    }
    // Choose which of our lands to return to hand. Preference, best first:
    //   (1) NEVER bounce another Karoo bounce land -- replaying it just triggers ANOTHER
    //       ETB bounce (a tempo-negative loop), so avoid it unless it is the only option;
    //   (2) prefer a land that is already TAPPED (spent this turn) so returning it costs no
    //       mana this turn -- the play-at-end timing (ApplyPlanDirect / AIEngine defer the
    //       Karoo until after the main casts) means the lands we needed are already tapped;
    //   (3) among those, prefer a land that ENTERS UNTAPPED when replayed (a basic / untapped
    //       dual) over one that enters tapped (a tapland / another Karoo), so the forced
    //       replay gives mana immediately rather than wasting next turn's tempo.
    // Legal returnable lands (controller's lands other than the karoo). WHICH one is provider-owned
    // (BounceLandCandidates); the base rule is the historical avoid-karoo / prefer-tapped /
    // prefer-enters-untapped weighting, so this is byte-identical. The karoo itself is the forced
    // fallback when it is the only land (the bounce is mandatory).
    std::vector<int> legal;
    for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
    {
        if (i == self_index) { continue; }
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != controller || !p.card.IsLand()) { continue; }
        legal.push_back(i);
    }
    int pick = -1;
    if (!legal.empty())
    {
        const std::vector<int> ranked =
            ResolveProvider(state).BounceLandCandidates(state, controller, self_index, legal);
        if (!ranked.empty()) { pick = ranked.front(); }
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

// ---- Flow-prune oracle (byte-identical infeasibility test) ------------------------------------
// Run ONCE at the top-level backtracker entry. The flat ManaPool::CanPay treats every flexible
// source as `wild` (any single pip), so an unpayable COLOUR pattern -- e.g. two {R/G} duals asked
// for {W}{W} -- slips the cheap affordability gate and drops to the exponential backtracker, which
// then spends ~2000 nodes PROVING the negative (measured: 97.5% of all backtracker nodes on the
// Creature Giving gen rollout are spent proving costs UNPAYABLE). This oracle proves such costs
// infeasible up front with a small max-flow (colours as a bipartite assignment), letting the worker
// return the SAME false without the proof -- byte-identical, since a failed payment leaves state
// unchanged either way and the DFS's first solution (when one exists) is never reached here.
//
// SAFETY: the oracle is a strict UNDER-approximation of feasibility. It models each untapped source
// as "one tap -> one mana of a fixed colour set" and each source's colour set as a SUPERSET of what
// it can really make (over-credit only). It prunes ONLY when even this generous model admits no
// assignment, and BAILS (never prunes) on any source it cannot model exactly this way (>1 mana per
// tap, filters, ramp-filters, domain, storage, scaled) or on a hybrid/{X} cost. So it can never
// prune a payable cost. Returns true => DEFINITELY infeasible (prune); false => feasible OR not
// modellable (fall through to the real backtracker).
//
// Flow graph (small, ~<=30 nodes, flow <= ManaValue): S -> src_i (cap 1) -> colour_c (cap 1) for
// each colour c the source may make; S -> colour_c (cap floating.c); S -> WILD (cap floating.wild)
// -> colour_c (cap INF, all six -- wild pays any pip incl {C}); colour_c -> T (cap strict demand:
// cost.white.. + colorless); colour_c -> GEN (cap INF; any colour pays a generic pip); GEN -> T
// (cap cost.generic). Feasible iff maxflow == cost.ManaValue().
static bool TapFlowInfeasible(const GameState& state, const ManaCost& cost, bool for_creature,
                              const ManaPool& floating, const std::vector<Color>* rp_colors,
                              std::uint64_t reserved_mask,
                              const std::vector<std::pair<int, const CardDefinition*>>& cands,
                              bool* out_bailed = nullptr)
{
    if (out_bailed) { *out_bailed = false; }
    // {X} and hybrid pips are outside this model (X is chosen elsewhere; hybrids need per-assignment
    // expansion) -> bail rather than risk an under-credit. cost.has_x with x_pips==0 is inert, but a
    // set has_x flag still signals an unresolved-X context, so stay conservative.
    if (cost.hybrid_count > 0 || cost.has_x) { if (out_bailed) { *out_bailed = true; } return false; }

    const int active = state.active_player_index;

    // Node layout: 0=S, 1=T, 2=GEN, 3=WILD, 4..9 = colour nodes (W,U,B,R,G,C = Color enum order),
    // 10.. = one per eligible untapped source. Colour node for Color c = 4 + int(c).
    constexpr int S = 0, T = 1, GEN = 2, WILD = 3, COL0 = 4, FIRST_SRC = 10;
    constexpr int INF = 1 << 20;

    struct FEdge { int to, cap, rev; };
    static thread_local std::vector<FEdge> g_edges;
    static thread_local std::vector<std::vector<int>> g_adj;
    g_edges.clear();
    // adjacency: reuse the outer vectors but clear each row we touch. Size to the max node count.
    auto ensure_nodes = [&](int n)
    {
        if (static_cast<int>(g_adj.size()) < n) { g_adj.resize(n); }
        for (int i = 0; i < n; ++i) { g_adj[i].clear(); }
    };
    auto add_edge = [&](int u, int v, int cap)
    {
        g_adj[u].push_back(static_cast<int>(g_edges.size())); g_edges.push_back({ v, cap, 0 });
        g_adj[v].push_back(static_cast<int>(g_edges.size())); g_edges.push_back({ u, 0, 0 });
    };

    // First pass: validate every eligible source fits the 1-tap->1-mana model and collect colour
    // sets. `rp_ready` mirrors the worker's lazy Reflecting-Pool union (computed once, aliased).
    struct SrcColset { std::uint8_t bits; int amt; };   // bit c => may make colour c; amt = mana/tap
    static thread_local std::vector<SrcColset> srcs;
    srcs.clear();
    bool rp_ready = (rp_colors != nullptr);
    for (const std::pair<int, const CardDefinition*>& cand : cands)
    {
        const int i = cand.first;
        if (state.battlefield[i].tapped) { continue; }
        if (reserved_mask & (1ull << i)) { continue; }
        const CardDefinition* def = cand.second;
        const bool is_src = (def->tmpl == CardTemplate::BasicLand)
                         || (def->tmpl == CardTemplate::ManaDork && CanTapNow(state.battlefield[i], state.battlefield))
                         || def->params.mana_rock;
        if (!is_src) { continue; }
        if (def->params.creature_mana_only && !for_creature) { continue; }
        if (!StorageSourceLive(state.battlefield[i], *def)) { continue; }
        if (!GraveyardFuelLive(state, active, *def)) { continue; }
        // Unmodellable source types -> the whole oracle bails (it would MIS-credit their yield): a
        // filter/ramp consumes+converts floating; domain makes one of EACH colour (not a choice);
        // storage bursts a variable amount; scaled pays a feeder for N-of-one. Every other source --
        // including a bounce/Karoo land (produces_amount>=2) -- is uniformly "one tap -> `amt` mana of
        // ONE chosen colour among `produces`" (the worker's loop below), so `amt` is its flow capacity.
        if (def->params.is_filter || def->params.ramp_filter || def->params.domain_mana
            || def->params.storage_land || IsScaledManaLand(*def))
        { if (out_bailed) { *out_bailed = true; } return false; }
        const int amt = ManaProducedPerTap(*def);   // 1 for a normal land/dork/rock; 2 for a Karoo

        // Colour set (SUPERSET, over-credit safe) -- mirror the worker's produces/reflecting/cco
        // handling. A drip land additionally offers a {C} mode, so add Colorless there.
        if (def->params.reflecting && !rp_ready)
        { rp_colors = &ReflectedColors(state, active, /*in_hand=*/false); rp_ready = true; }
        const std::vector<Color>& produces = def->params.reflecting ? *rp_colors : def->params.produces;
        std::uint8_t bits = 0;
        if (def->params.colored_creature_only && !for_creature)
        {
            bits |= (1u << static_cast<int>(Color::Colorless));   // only its {C} survives
        }
        else
        {
            for (Color c : produces) { bits |= (1u << static_cast<int>(c)); }
            if (bits == 0 && !def->params.reflecting)
            { bits |= (1u << static_cast<int>(Color::Colorless)); }   // {C}-only source (empty produces)
            if (def->params.tap_opponent_lifegain > 0)
            { bits |= (1u << static_cast<int>(Color::Colorless)); }   // drip land's {C} mode
        }
        if (bits == 0) { continue; }   // solo Reflecting Pool etc.: makes nothing usable -> no supply
        srcs.push_back({ bits, amt });
    }

    const int m = static_cast<int>(srcs.size());
    const int demand = cost.ManaValue();
    if (demand == 0) { if (out_bailed) { *out_bailed = true; } return false; }   // nothing to pay (unreachable)

    ensure_nodes(FIRST_SRC + m);

    // Strict colour demand (colours that must be paid by their own colour, or by wild).
    const int strict[6] = { cost.white, cost.blue, cost.black, cost.red, cost.green, cost.colorless };
    const int fl[6] = { floating.white, floating.blue, floating.black, floating.red, floating.green,
                        floating.colorless };
    for (int c = 0; c < 6; ++c)
    {
        if (strict[c] > 0)  { add_edge(COL0 + c, T, strict[c]); }   // colour_c pays its own strict demand
        add_edge(COL0 + c, GEN, INF);                               // excess colour pays generic
        if (fl[c] > 0)      { add_edge(S, COL0 + c, fl[c]); }       // floating of colour c
        add_edge(WILD, COL0 + c, INF);                              // wild pays any colour
    }
    if (cost.generic > 0) { add_edge(GEN, T, cost.generic); }
    if (floating.wild > 0) { add_edge(S, WILD, floating.wild); }
    for (int s = 0; s < m; ++s)
    {
        add_edge(S, FIRST_SRC + s, srcs[s].amt);   // one tap yields `amt` mana (1, or 2 for a Karoo)
        for (int c = 0; c < 6; ++c)
        { if (srcs[s].bits & (1u << c)) { add_edge(FIRST_SRC + s, COL0 + c, srcs[s].amt); } }
    }

    // Edmonds-Karp: BFS augmenting paths until saturated or no path. demand is small (<= ~15).
    const int N = FIRST_SRC + m;
    static thread_local std::vector<int> prev_edge, bfsq;
    int flow = 0;
    while (flow < demand)
    {
        prev_edge.assign(N, -1);
        bfsq.clear();
        bfsq.push_back(S);
        prev_edge[S] = -2;   // visited marker for the source
        for (std::size_t qi = 0; qi < bfsq.size() && prev_edge[T] == -1; ++qi)
        {
            const int u = bfsq[qi];
            for (int eid : g_adj[u])
            {
                const FEdge& e = g_edges[eid];
                if (e.cap > 0 && prev_edge[e.to] == -1)
                { prev_edge[e.to] = eid; bfsq.push_back(e.to); }
            }
        }
        if (prev_edge[T] == -1) { break; }   // no augmenting path
        // Bottleneck along the path.
        int bott = INF;
        for (int v = T; v != S; )
        { const FEdge& e = g_edges[prev_edge[v]]; bott = std::min(bott, e.cap); v = g_edges[prev_edge[v] ^ 1].to; }
        for (int v = T; v != S; )
        { g_edges[prev_edge[v]].cap -= bott; g_edges[prev_edge[v] ^ 1].cap += bott; v = g_edges[prev_edge[v] ^ 1].to; }
        flow += bott;
    }
    return flow < demand;   // could not saturate the full cost -> DEFINITELY infeasible
}

// Recursive worker. The public TapForCostBacktrack (below) is a thin wrapper so it can record the
// payable/unpayable OUTCOME split under MTG_TAP_STATS; the recursion calls the WORKER directly, so a
// wrapper call is always one top-level entry. Behaviour is identical to calling the worker directly.
static bool TapForCostBacktrackWorker(GameState& state, const ManaCost& cost,
                                bool for_creature, ManaPool floating,
                                const std::vector<Color>* rp_colors,
                                TapBacktrackMemo* fail_memo,
                                ManaPool* out_leftover,
                                std::uint64_t tapped_mask,
                                int untapped_max,
                                std::uint64_t reserved_mask,
                                ManaPool* out_full_pool,
                                const std::vector<std::pair<int, const CardDefinition*>>* src_cands)
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

    // Upper bound on one source's net mana, state-aware for the scaled land (SourceMaxNet has no
    // GameState, so it under-counts Three Tree City's board-scaled net -> a losslessness violation
    // that could prune a payable scaled line). The B&B gate needs an OVER-count, so take the larger
    // of the static bound and the scaled net (N - feeder). Identity for every non-scaled source.
    auto source_max_net = [&](const Permanent& pp, const CardDefinition& dd) -> int
    {
        int b = SourceMaxNet(pp, dd);
        if (IsScaledManaLand(dd))
        { b = std::max(b, ScaledManaCreatureCount(state) - dd.params.mana_per_creature_feeder_generic); }
        // Domain source (Faeburrow / Bloom Tender): one tap yields |domain| mana (2-5), but the
        // static bound reads ManaProducedPerTap = 1 -- the under-count made this LOSSY: the gate
        // pruned payable WUBRG costs and the executor silently dropped legal casts the search had
        // committed (the FiveColour claude-play sweep's convergent finding, 14/18 games).
        if (dd.params.domain_mana)
        { b = std::max(b, static_cast<int>(EffectiveProduces(state, active, dd).size())); }
        return b;
    };

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
    if (tapstats::Enabled())
    {
        tapstats::g_nodes.fetch_add(1, std::memory_order_relaxed);   // every recursion node
        if (top_level)
        {
            tapstats::g_backtrack_entries.fetch_add(1, std::memory_order_relaxed);
            if (n > 64) { tapstats::g_top_memo_off.fetch_add(1, std::memory_order_relaxed); }
            std::uint64_t prev = tapstats::g_max_n.load(std::memory_order_relaxed);
            while (static_cast<std::uint64_t>(n) > prev &&
                   !tapstats::g_max_n.compare_exchange_weak(prev, static_cast<std::uint64_t>(n),
                                                            std::memory_order_relaxed)) {}
        }
    }
    // Reuse the failure-memo's bucket allocation across the (hundreds of thousands of) top-level calls
    // instead of constructing/destructing a fresh unordered_set each time. clear() retains the bucket
    // array (libstdc++), so after warm-up there is no per-call bucket alloc/free and no rehash growth --
    // which the profile showed as a real slice (operator new/delete + _M_rehash) on a token-heavy deck
    // where the greedy strands ~400k times per rollout. Byte-identical: each top-level call sees an
    // empty memo and inserts exactly the same proven-failure keys, so every prune is unchanged. Safe as
    // thread_local (each gen worker has its own) because no nested top-level backtrack runs in a subtree.
    static thread_local TapBacktrackMemo s_memo_tl;
    if (top_level && n <= 64) { s_memo_tl.clear(); fail_memo = &s_memo_tl; }

    // Structural mana-source list (the controller's BasicLand / ManaDork / mana_rock permanents +
    // their cached def), enumerated ONCE at the top-level call and threaded to every node. The
    // recursion then iterates ~sources instead of the whole battlefield and never re-runs the
    // LookupCached hash: a token-flooded OPPONENT board (Forbidden Orchard / Hunted Phantasm /
    // Varchild's) is skipped entirely instead of rescanned at each of the millions of nodes. This is
    // a SUPERSET of the per-node `is_src` structural test (ManaDork's CanTapNow check stays per-node),
    // and the battlefield is invariant during a payment, so it is byte-identical. thread_local buffer:
    // rebuilt at each top-level call, reused via the threaded pointer by that call's subtree (no nested
    // top-level backtrack exists within a subtree, so the buffer is never clobbered mid-iteration).
    static thread_local std::vector<std::pair<int, const CardDefinition*>> s_src_cands_buf;
    if (top_level)
    {
        s_src_cands_buf.clear();
        for (int i = 0; i < n; ++i)
        {
            const Permanent& p = state.battlefield[i];
            if (p.controller_index != active) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
            if (!d) { continue; }
            if (d->tmpl == CardTemplate::BasicLand || d->tmpl == CardTemplate::ManaDork
                || d->params.mana_rock)
            { s_src_cands_buf.push_back({ i, d }); }
        }
        src_cands = &s_src_cands_buf;
    }
    const std::vector<std::pair<int, const CardDefinition*>>& cands = *src_cands;

    // Identical-source sibling collapse. On a fanned-out board (a dozen Treasures from copied Gold
    // Rushes, a dork chain) the failure memo's per-permanent bitmask keys treat every same-named,
    // same-state source as distinct, so proving one unpayable cost walks C(k,j) masks per tapped
    // count -- the 14-hour TapForCostBacktrackWorker blow-up (docs/design/tap-backtrack-blowup.md).
    // But two sources with the same CardDefinition and the same payment-relevant permanent state
    // (counters, storage battery + hold flag; tap-eligibility folded into the chain below) have
    // ISOMORPHIC subtrees: tapping either yields states differing only in WHICH index bears the
    // tapped flag, and nothing in a payment reads that index except the (perf-only) memo mask. So at
    // any node, once the first such source has been explored and failed, its identical siblings must
    // fail identically -- skip them. This collapses the reachable tapped-set space from 2^k toward
    // (count+1) per identical class, byte-identically, by the colour-collapse argument above: only
    // subtrees isomorphic to PROVEN FAILURES are skipped (and failures restore all state), so the
    // first payment found -- and the exact sources it leaves tapped -- is unchanged.
    //
    // s_dup_of_buf[ci] chains candidate ci to the nearest EARLIER chain-eligible identical candidate
    // (-1 = none). At a node, a chain member that is currently UNTAPPED was reached by this node's
    // loop before ci (same order, and failed branches restore state between iterations), passed the
    // same def-and-sig-determined filters, and so was explored (or itself dup-skipped, which by
    // induction chains to an explored one) and FAILED -> skip ci. A member currently tapped is on
    // the DFS path (tapped by an ancestor, not explorable at THIS node) -> walk past it. Chain
    // eligibility (untapped at entry, unreserved, dork can tap) is computed ONCE here: CanTapNow is
    // invariant during a payment (haste grants never read `tapped`, no permanent enters or leaves),
    // and a source tapped at entry stays tapped throughout. MTG_NO_TAP_DUP_COLLAPSE=1 disables it
    // (standing A/B lever on one binary; output must be byte-identical either way).
    static thread_local std::vector<int> s_dup_of_buf;
    if (top_level)
    {
        static const bool s_no_dup_collapse = EnvOn("MTG_NO_TAP_DUP_COLLAPSE");
        const std::size_t nc = cands.size();
        s_dup_of_buf.assign(nc, -1);
        if (!s_no_dup_collapse)
        {
            auto counters_equal = [](const std::vector<Counter>& a, const std::vector<Counter>& b)
            {
                if (a.size() != b.size()) { return false; }
                for (std::size_t k = 0; k < a.size(); ++k)
                { if (a[k].type != b[k].type || a[k].count != b[k].count) { return false; } }
                return true;
            };
            auto chain_eligible = [&](const std::pair<int, const CardDefinition*>& c) -> bool
            {
                const Permanent& p = state.battlefield[c.first];
                if (p.tapped) { return false; }                        // tapped at entry: never explorable
                if (reserved_mask & (1ull << c.first)) { return false; }
                if (c.second->tmpl == CardTemplate::ManaDork && !CanTapNow(p, state.battlefield))
                { return false; }
                return true;
            };
            for (std::size_t ci = 1; ci < nc; ++ci)
            {
                const Permanent& pa = state.battlefield[cands[ci].first];
                for (std::size_t cj = ci; cj-- > 0; )
                {
                    if (cands[cj].second != cands[ci].second || !chain_eligible(cands[cj]))
                    { continue; }
                    const Permanent& pb = state.battlefield[cands[cj].first];
                    if (pb.storage_counters != pa.storage_counters
                        || pb.storage_hold_this_turn != pa.storage_hold_this_turn
                        || !counters_equal(pb.counters, pa.counters)) { continue; }
                    s_dup_of_buf[ci] = static_cast<int>(cj);
                    break;
                }
            }
        }
    }

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
                                 || (d->tmpl == CardTemplate::ManaDork && CanTapNow(p, state.battlefield))
                                 || d->params.mana_rock;
                if (!is_src) { continue; }
                if (d->params.creature_mana_only && !for_creature) { continue; }
                if (!StorageSourceLive(p, *d)) { continue; }   // uncharged storage land makes no mana
                if (!GraveyardFuelLive(state, active, *d)) { continue; }   // Deathrite: no gy land
                untapped_max += source_max_net(p, *d);
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

    // Flow-prune oracle: prove this cost contention-infeasible up front so we return the same false
    // without walking the exponential tree to prove it (byte-identical -- see TapFlowInfeasible). Run
    // ONLY at the top level (it examines all untapped sources fresh) and only when the fail-memo is
    // active (n<=64); a deep node's partial-tap state is already handled by the memo + B&B gate.
    if (top_level && fail_memo && FlowPruneEnabled())
    {
        bool bailed = false;
        const bool infeasible = TapFlowInfeasible(state, cost, for_creature, floating, rp_colors,
                                                  reserved_mask, cands, tapstats::Enabled() ? &bailed : nullptr);
        if (infeasible)
        {
            if (tapstats::Enabled()) { tapstats::g_flow_prune.fetch_add(1, std::memory_order_relaxed); }
            fail_memo->insert(key);   // consistency with the B&B gate's prune (harmless at top level)
            return false;
        }
        if (tapstats::Enabled() && bailed) { tapstats::g_flow_bail.fetch_add(1, std::memory_order_relaxed); }
    }

    // Colour-branch collapse. A multi-colour source (Birds of Paradise: W/U/B/R/G) spawns one recursion
    // per producible colour. But the `cost` is INVARIANT for the whole backtrack, so a produced colour
    // the cost never demands as a coloured pip can only ever be spent as GENERIC -- and one generic
    // mana is interchangeable with any other for every downstream CanPay of this fixed cost. Tapping
    // Birds for W vs U vs B vs R therefore explores identical subtrees (same success/failure at every
    // node, same set of sources left tapped -- colour choice is not stored on the permanent, and the
    // drip/self-damage side effects are colour-independent); only the floating pool's colour
    // composition differs -- and the first solution found is unchanged (the representative is the FIRST
    // generic-only colour in produces order, and an interchangeable colour's subtree is isomorphic:
    // identical success/failure at every node and the same set of sources left tapped, so the total
    // mana produced is identical, only recoloured). We fold all generic-only colours of each source
    // into a single representative branch and skip the rest -- cutting the branching factor on
    // Birds+flexible-land boards (the gen hotspot: 5^k tap orderings collapse toward the colours the
    // cost actually needs). `special_mask` marks the colours the cost DOES demand (a mono pip, a {C}
    // pip, or either half of a hybrid pip); those keep their own branch. Colorless is always special (a
    // {C}-only source produces a single colour anyway, so nothing to collapse -- and it keeps {C}-pip
    // payment exact).
    //
    // GUARD (out_leftover == nullptr). The recoloured pool is observable only through the surfaced-pool
    // outputs, so collapse is byte-identical exactly when no caller inspects a produced colour:
    //   * out_leftover (ManaPayment): the over-produced remainder is FLOATED and later drained
    //     colour-sensitively (a subsequent cast's coloured pip), so a recolour could diverge -- UNSAFE.
    //     (This path also gets ~0 benefit: its per-cast costs rarely leave Birds' colours generic-only.)
    //   * out_full_pool (TurnSolver batch prepay): the caller reads ONLY produced.wild (== 0 check;
    //     collapse never adds wild) and produced.Total() (unchanged by recolour) -- it rebuilds the
    //     floating pool from `combined`'s pinned pips + wild=Total-pinned and DISCARDS produced's
    //     colours entirely (TurnSolver.cpp ~6221-6232). SAFE, and this path is the whole win.
    // So collapse is enabled whenever out_leftover is null (covers both-null and full-pool-only); any
    // future out_full_pool caller that reads a per-colour count must keep this contract.
    // MTG_NO_COLOR_COLLAPSE=1 makes the collapse inert, for a standing A/B on ONE binary (the repo
    // rule: never let an A/B straddle a build.sh). Byte-identical either way -- the arms differ in
    // work done, not in the payment found -- so a digest diff between them is a bug, not a result.
    static const bool s_no_collapse = EnvOn("MTG_NO_COLOR_COLLAPSE");
    const bool collapse_colors = (out_leftover == nullptr) && !s_no_collapse;
    unsigned special_mask = 0;
    if (collapse_colors)
    {
        if (cost.white > 0) { special_mask |= 1u << static_cast<int>(Color::White); }
        if (cost.blue  > 0) { special_mask |= 1u << static_cast<int>(Color::Blue); }
        if (cost.black > 0) { special_mask |= 1u << static_cast<int>(Color::Black); }
        if (cost.red   > 0) { special_mask |= 1u << static_cast<int>(Color::Red); }
        if (cost.green > 0) { special_mask |= 1u << static_cast<int>(Color::Green); }
        special_mask |= 1u << static_cast<int>(Color::Colorless);   // {C} pips need colourless mana
        for (int h = 0; h < cost.hybrid_count; ++h)
        {
            special_mask |= 1u << (cost.hybrid_pair[h] >> 4);
            special_mask |= 1u << (cost.hybrid_pair[h] & 0xF);
        }
    }

    for (std::size_t ci = 0; ci < cands.size(); ++ci)
    {
        const std::pair<int, const CardDefinition*>& cand = cands[ci];
        const int i = cand.first;
        if (state.battlefield[i].tapped) { continue; }
        if (reserved_mask & (1ull << i)) { continue; }   // reservation audit: this source is held (not tappable)
        const CardDefinition* def = cand.second;   // hoisted: controller==active filter + LookupCached done once
        const bool is_src = (def->tmpl == CardTemplate::BasicLand)
                         || (def->tmpl == CardTemplate::ManaDork && CanTapNow(state.battlefield[i], state.battlefield))
                         || def->params.mana_rock;
        if (!is_src) { continue; }
        if (def->params.creature_mana_only && !for_creature) { continue; }
        if (!StorageSourceLive(state.battlefield[i], *def)) { continue; }   // uncharged storage: no mana
        if (!GraveyardFuelLive(state, active, *def)) { continue; }   // Deathrite: no gy land = no mana

        // Identical-sibling collapse (see the s_dup_of_buf block above): an untapped chain member was
        // explored earlier at this node -- with the exact same def, permanent state and filters -- and
        // failed, so this candidate's subtree is isomorphic to a proven failure. Skip it. Members
        // currently tapped are on the DFS path (not explored at THIS node) -> walk past them.
        {
            bool dup = false;
            for (int j = s_dup_of_buf[ci]; j >= 0; j = s_dup_of_buf[j])
            { if (!state.battlefield[cands[j].first].tapped) { dup = true; break; } }
            if (dup)
            {
                if (tapstats::Enabled()) { tapstats::g_dup_skips.fetch_add(1, std::memory_order_relaxed); }
                continue;
            }
        }

        // Reflecting Pool -> the shared, hoisted union (empty = solo RP = no mana); every other
        // source -> its static produces[]. (Inlined EffectiveProduces so the union is reused.)
        if (def->params.reflecting && !rp_ready)
        { rp_colors = &ReflectedColors(state, active, /*in_hand=*/false); rp_ready = true; }
        const std::vector<Color>& produces_base = def->params.reflecting ? *rp_colors : def->params.produces;
        // colored_creature_only (Unclaimed Territory / Cavern of Souls): a non-creature spell may take
        // only {C} from this source (its coloured mana is creature-only). Strip the colours here so the
        // pip-matching below can pay a generic pip with {C} but never a coloured pip. Keeps the reflecting
        // hoist above (these lands are never reflecting). Identity for every other source -> byte-identical.
        static thread_local std::vector<Color> ccov;
        const std::vector<Color>* produces_ptr = &produces_base;
        if (def->params.colored_creature_only && !for_creature)
        {
            ccov.clear();
            for (Color c : produces_base) { if (c == Color::Colorless) { ccov.push_back(c); } }
            produces_ptr = &ccov;
        }
        const std::vector<Color>& produces = *produces_ptr;
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
        const int storage_snap = state.battlefield[i].storage_counters;   // burst zeroes it (undo below)
        const int src_max_net  = source_max_net(state.battlefield[i], *def); // captured pre-tap (storage_snap-aware)
        const int life_snap = state.players[active].life;
        const int opp_life_snap = state.players[1 - active].life;   // for tap_opponent_lifegain undo
        const bool oll_snap = state.opponent_lost_life_this_turn;

        // Physically tap source i, recurse with `next` floating, undo on failure. `drip_ok` is
        // false for a Grove-style drip land's painless "{T}: Add {C}" branch (a generic pip absent
        // a Remedy) so it does not pay the opponent life; its {R}/{G} branches leave it true.
        // `storage_burn` (> 0 only for a storage-counter land) is how many counters this tap removes:
        // the PARTIAL shortfall, not all of them, so the rest persist (mirrors the greedy tap_source).
        auto activate = [&](const ManaPool& next, bool drip_ok = true, int storage_burn = 0) -> bool
        {
            state.battlefield[i].tapped = true;
            DecrementDepletionOnTap(state.battlefield[i]);
            if (def->params.storage_land) { state.battlefield[i].storage_counters -= storage_burn; }
            if (def->params.tap_self_damage > 0)
            { state.players[active].life -= def->params.tap_self_damage; }
            // Deathrite Shaman: the tap exiles a graveyard land (eligibility guaranteed one).
            // Remember which slot so a failed branch re-inserts it exactly (byte-identical undo).
            // gy_exiled_card is lazy (std::optional): a plain `Card` here default-constructs an
            // InternedName on EVERY activate() -- calling InternedName::EmptyStr() 39.8M x in a
            // token-heavy rollout (~1.9% self-cost in callgrind) -- even though it is only ever used
            // on the Deathrite (gy_land_exile_mana) path below. optional constructs no Card until the
            // exile actually happens, so the common path pays nothing. Byte-identical.
            int gy_exiled_at = -1; std::optional<Card> gy_exiled_card;
            if (def->params.gy_land_exile_mana)
            {
                std::vector<Card>& gy = state.players[active].graveyard;
                for (std::size_t g = 0; g < gy.size(); ++g)
                {
                    // ZoneCard: a graveyard card is a name-only placeholder with an EMPTY type mask,
                    // so the raw gy[g].IsLand() here was always false -- this loop never exiled
                    // anything. The fuel gate (GraveyardFuelLive) said "live" and the tap took the
                    // mana, but the land stayed in the graveyard, so N Deathrites could all tap off
                    // ONE graveyard land in a single payment (this deck runs four). The pool builders
                    // were already correct (they cap credited sources at GraveyardLandFuel); only this
                    // inline copy of ExileGraveyardLandForMana had the raw read.
                    if (ZoneCard(gy[g]).IsLand())
                    {
                        gy_exiled_at = static_cast<int>(g);
                        gy_exiled_card = gy[g];
                        gy.erase(gy.begin() + static_cast<std::ptrdiff_t>(g));
                        break;
                    }
                }
            }
            // Grove of the Burnwillows drip (opponent gains -> loses with Remedy). Restored
            // below on failure alongside the active player's life.
            if (drip_ok && def->params.tap_opponent_lifegain > 0)
            { OpponentGainsLife(state, active, def->params.tap_opponent_lifegain); }
            if (TapForCostBacktrackWorker(state, cost, for_creature, next, rp_colors, fail_memo, out_leftover,
                                    tapped_mask | (1ull << i),
                                    untapped_max < 0 ? -1 : untapped_max - src_max_net,
                                    reserved_mask, out_full_pool, src_cands)) { return true; }
            state.battlefield[i].tapped = tapped_snap;   // only this source was touched at this level
            if (has_counters) { state.battlefield[i].counters = counters_snap; }
            state.battlefield[i].storage_counters = storage_snap;
            if (gy_exiled_at >= 0)
            {
                std::vector<Card>& gy = state.players[active].graveyard;
                gy.insert(gy.begin() + static_cast<std::ptrdiff_t>(gy_exiled_at), *gy_exiled_card);
            }
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
            // Storage-counter land: burst only the PARTIAL shortfall (cost minus what this branch has
            // already floated), removing that many counters; the rest persist. Every other source uses
            // its static per-tap yield. `storage_burn` tells activate how many counters to remove.
            const int storage_burn = def->params.storage_land
                ? std::min(state.battlefield[i].storage_counters,
                           std::max(1, cost.ManaValue() - floating.Total()))
                : 0;
            // Domain source (Faeburrow / Bloom Tender): one tap yields one mana of EACH colour
            // among controlled permanents -- handled as its own branch (the single-colour loop
            // below would misprice it as amt-of-one-colour). MUST use the DYNAMIC domain
            // (EffectiveProduces -> DomainColors), not the static `produces` hint (WUBRG): the
            // static list both over-credits colours the board doesn't have (an illegal payment
            // the greedy path would never make) and diverges from tap_source/the enumeration.
            if (def->params.domain_mana)
            {
                const std::vector<Color>& dom = EffectiveProduces(state, active, *def);
                if (!dom.empty())
                {
                    ManaPool f = floating;
                    for (Color c : dom) { f.Add(c, 1); }
                    if (activate(f)) { return true; }
                }
                continue;
            }
            const int amt = def->params.storage_land ? storage_burn : ManaProducedPerTap(*def);
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
                { ManaPool f = floating; f.Add(Color::Colorless, amt); if (activate(f, /*drip_ok=*/false, storage_burn)) { return true; } }
                bool generic_rep_done = false;   // collapse: explore one generic-only colour, skip the rest
                for (Color c : produces)
                {
                    if (collapse_colors && !(special_mask & (1u << static_cast<int>(c))))
                    {
                        if (generic_rep_done) { continue; }   // interchangeable generic mana already tried
                        generic_rep_done = true;
                    }
                    CcoAuditTap(*def, c, for_creature);   // audit: `produces` is cco-stripped above
                    ManaPool f = floating; f.Add(c, amt); if (activate(f, /*drip_ok=*/true, storage_burn)) { return true; }
                }
            }
            // Three Tree City scaled mode: "{feeder},{T}: add N of a chosen colour" (N = creatures you
            // control). Offered IN ADDITION to the basic {C} tap above -- reached only if the basic tap
            // did not lead to a payment. Requires >= {feeder} already-produced floating (fed by prior
            // taps in this DFS ordering, like a ramp filter); consumes the {feeder} generic, then adds
            // N of ONE chosen colour (branch over all five). Net (N - feeder) mana. Empty subtype (every
            // non-scaled source) -> never entered, so byte-identical off Three Tree.
            if (IsScaledManaLand(*def))
            {
                const int feeder = def->params.mana_per_creature_feeder_generic;
                const int give   = ScaledManaCreatureCount(state);        // N of the chosen colour
                if (give >= 1 && floating.Total() >= feeder)
                {
                    const Color cols[5] = { Color::White, Color::Blue, Color::Black, Color::Red, Color::Green };
                    for (Color c : cols)
                    {
                        ManaPool f = floating; bool ok = true;
                        for (int k = 0; k < feeder; ++k) { Color took; if (!ConsumeFloatingAny(f, took)) { ok = false; break; } }
                        if (!ok) { break; }
                        f.Add(c, give);
                        if (activate(f)) { return true; }
                    }
                }
            }
        }
    }
    // Every option from this (tapped-set, floating) state was exhausted without paying -> record the
    // proven failure so a different tap ordering reaching the same state short-circuits instead of
    // re-exploring. State is unchanged here (the invariant), so this is byte-identical.
    if (fail_memo) { fail_memo->insert(key); }
    return false;
}

// ============================ PAYABLE MANA CACHE (MTG_MANA_CACHE) ============================
// Memoises the out_full_pool batch-prepay solve. Measured: within a rollout the search re-queries the
// SAME (source-config, cost) ~80% of the time, and each deep solve is 20-40 backtracker nodes -- so
// caching the first solution and replaying it skips the search on the repeats. The map holds the DFS's
// determining inputs -> {payable, produced pool, tap-set}; a hit re-taps the stored sources and returns
// the pool. BYTE-IDENTICAL by construction: the backtracker is a pure deterministic function of the key,
// so a hit reproduces the same first solution (128-bit key -> false-match prob ~2^-94; validated
// byte-identical across the full regression suite -- 50/50 digests, 0 play-changed, 184 refs 0 drift --
// before adoption). Default ON (MTG_MANA_CACHE=0 disables). Correctness gates:
//   * SHAPE: canonical batch-prepay only -- out_full_pool set, out_leftover/rp_colors null, empty
//     floating, board <= 64 permanents (tap-set fits a bitmask, matching the fail-memo's own n<=64 cap).
//   * SCALING sources are HASHED, not skipped (2026-08-14). Reflecting Pool always was: its effective
//     colour set (a function of the other lands) goes into the key, so different reachable-colour
//     boards get distinct entries. Domain (colours among ALL permanents) and scaled (creature count)
//     now do the same -- the realised colour SET / creature COUNT is hashed once per key. Both are
//     invariant across a solve (payment only taps; nothing enters, leaves or changes colour), so the
//     value read at key-build time is the value every branch reads. Before this they DISABLED the
//     cache board-wide, which turned the single card that makes payment expensive into the card that
//     switched the memo off -- see McDomain/McScaled below for the measurement.
//   * STORE (McReplayable): a MISS's solution is cached only if every source it TAPS can be REPLAYED by
//     the hit path -- i.e. its tap effect is reproduced when we re-set .tapped. Simple lands/dorks/rocks
//     qualify; City of Brass qualifies (its tap_self_damage is replayed on the hit); a DEPLETION land
//     (Sandstone Needle) qualifies since 2026-08-12 -- its tap's only side effect is
//     DecrementDepletionOnTap, which the hit replays exactly (production is counter-independent, and
//     the decrement is relative, so no counter value needs to enter the key). Storage, drip
//     (tap_opponent_lifegain) and Deathrite (gy_land_exile_mana) lands do NOT -- storage/drip mutate
//     counters/opponent life the hit path does not reproduce, and Deathrite's tap EXILES a graveyard
//     land the hit path cannot replay from a tap-set alone -- so a solution tapping one is left
//     unstored (recomputed by the real DFS each time). Negative (unpayable) results are always
//     cacheable (no side effect). A cache MISS always runs the real DFS, so a non-storable solution is
//     still correct; it just is not memoised.
namespace {
inline bool ManaCacheEnabled() { static const bool v = EnvOn("MTG_MANA_CACHE", true); return v; }
// EVERY source is replayable (2026-08-14). The store gate used to reject any solution that tapped a
// storage, drip or Deathrite source, on the grounds that a tap-set alone cannot reproduce their side
// effects. That threw away half the work on a deck that plays one: 49.6% of FiveColour's cache misses
// solved a payment, paid for it, and then could not keep the answer (four Deathrite Shamans).
//
// A tap-set alone indeed cannot -- but a tap-set plus the two ORDER-DEPENDENT choices can, and the
// rest resolve themselves if the replay CALLS THE SAME FUNCTIONS the DFS called instead of storing
// deltas. The full side-effect surface of the DFS's activate() is: tapped, DecrementDepletionOnTap,
// storage counter burn, tap_self_damage, graveyard-land exile, opponent drip. Of those:
//   * storage BURN depends on the floating pool at that point in the tap ORDER -- not derivable, so
//     it is recorded per tap (as a counter DELTA, which is also how a partial burn stays partial).
//   * drip depends on which colour branch was taken ({C} mode does not drip) -- recorded as one
//     aggregate amount, exact because life arithmetic is order-independent and only drip moves the
//     opponent's life during a payment.
//   * the graveyard EXILE re-runs ExileGraveyardLandForMana on the hit, which picks the same
//     first-match the DFS's inline copy would pick ON THIS board. That is the point: the key hashes
//     the fuel COUNT, not the graveyard's contents, so two boards with the same count share an entry
//     -- and each replays ITS OWN first land, which is exactly what the DFS would have done to it.
//   * likewise the drip's SIGN: OpponentGainsLife re-reads RemedyActive, so a Tainted Remedy board
//     flips the gain to a loss on the hit path without the key ever having to see the enchantment.
// The rule this rests on: replay by CALLING, not by storing a delta. Anything a replayed function
// reads is then resolved against the state the hit is actually landing on.
struct ManaCacheTap
{
    std::uint64_t desc;   // canonical mode: the source's descriptor (0 in indexed mode)
    int idx;              // indexed mode: battlefield index. canonical mode: RANK within the group
    int storage_burn;     // counters this tap removed (0 for every non-storage source)
};
// SCALING sources -- domain (Faeburrow Elder / Bloom Tender: one mana of each colour among ALL your
// permanents) and scaled (Three Tree City: N of one colour, N = creatures you control). Both read
// board state the per-source key loop below cannot see, because the quantity ranges over permanents
// that are not mana sources at all (a creature's colour, a creature's existence).
//
// This USED to return true from ManaCacheKey -- i.e. one Bloom Tender on the battlefield disabled the
// payment cache for the WHOLE board, every solve, for the rest of the game. That is backwards: a
// domain source is precisely the card that makes payment enumeration expensive (its tap yields 2-5
// mana at once, so it multiplies the orderings the DFS explores), and it was the card that turned the
// memo off. FiveColour's mulligan scout measured the consequence -- Bloom Tender appeared in 78.3% of
// the degenerate (>=30 s) rollouts, Faeburrow Elder its top co-occurrence, worst single rollout 31
// minutes -- and an independent `perf` profile of the depth-matrix tail put mana payment at the top
// of a flat profile from a completely different measurement path.
//
// The fix is the one `reflecting` already uses: don't bail, HASH THE REALISED QUANTITY. Both are
// invariant across a solve (a payment only TAPS permanents -- nothing enters, leaves or changes
// colour), so the value read at key-build time is the value every DFS branch will read, and two
// boards that differ in it get different keys. Everything else these paths touch is already covered:
// ScaledManaFeederMana reads only mana sources' (index, def, tapped) plus graveyard fuel.
// DEFAULT ON; MTG_MANA_CACHE_SCALING=0 restores the old bail-out (kept as the A/B arm that measured
// this, and as the escape hatch if a future scaling source is ever added whose yield is NOT invariant
// under tapping -- that one would have to bail, or hash whatever it does read).
inline bool McScalingHashed()
{ static const bool v = EnvOn("MTG_MANA_CACHE_SCALING", true); return v; }
inline bool McDomain(const CardDefinition* d)  { return d && d->params.domain_mana; }
inline bool McScaled(const CardDefinition* d)  { return d && IsScaledManaLand(*d); }

struct ManaCacheEntry
{
    std::uint64_t             verify;
    bool                      payable;
    ManaPool                  produced;    // out_full_pool: the concrete mana the tap set makes
    ManaPool                  leftover;    // out_leftover: the over-production surfaced for floating
    std::vector<ManaCacheTap> taps;
    int                       drip_life;   // total opponent-life amount the solve's drip taps applied
};
inline thread_local std::unordered_map<std::uint64_t, ManaCacheEntry> g_mana_cache;

// DEFAULT ON; MTG_MANA_CACHE_ALLSRC=0 restores the old McReplayable store gate (simple sources only).
// The A/B arm that measured the Deathrite/storage/drip extension, and the hatch if a future source
// gains a tap effect this replay does not cover.
inline bool McAllSources() { static const bool v = EnvOn("MTG_MANA_CACHE_ALLSRC", true); return v; }

// DEFAULT ON; MTG_MANA_CACHE_LEFTOVER=0 restricts the cache back to the batch-prepay shape only.
//
// The cache used to cover ONE of the engine's two payment questions. TurnSolver's batch prepay ("can
// I afford this whole plan?", out_full_pool) was cached; TapForCostSharedOnce's per-cast fallback
// ("pay this one cost, tell me the leftover", out_leftover, sometimes fed by a ritual's floating
// reserve) was not -- 17.9% of calls but 55% of all payment NODES, re-solved from scratch every time.
//
// Covering it is the right shape for a second reason (user, 2026-08-14): the alternative -- widening
// the batch prepay so it maps the whole turn -- pre-floats all the mana, and pre-floating is actively
// WRONG for a scaling source. Bloom Tender taps for one mana per colour you control, so tapping it up
// front banks TODAY's colour count and throws away the extra mana it would have made after the next
// cast resolves; the same pre-float also spends mana a later phase may want held. Memoising the
// per-cast solves keeps payment lazy and in-order -- each subset paid when it is actually due -- and
// makes the repeats free, which is what was actually expensive.
inline bool McLeftoverShape() { static const bool v = EnvOn("MTG_MANA_CACHE_LEFTOVER", true); return v; }

// ---- CANONICAL (type-multiset) keying -----------------------------------------------------------
// The key used to identify each mana source by BATTLEFIELD INDEX, so two untapped Mountains posed the
// same payment problem under two different keys and a wide board re-solved it once per permutation of
// which copy was which. Measured on FiveColour d3: keying by type instead lifts the hit rate 83.6% ->
// 94.0%, i.e. 2.73x fewer solves -- and since the leftover-shape fix, misses are 100% of payment
// nodes, so that is 2.73x off the payment DFS.
//
// The equivalence is NOT invented here: it is exactly the one the DFS's own identical-sibling collapse
// (s_dup_of_buf) uses -- same def, same chain-eligibility (untapped at entry, unreserved, a dork that
// can tap), same storage counters/hold, same full counters vector. Reusing it is what makes the
// realisation sound, because that collapse is also what guarantees the DFS explores a group's members
// in ASCENDING INDEX order: at any node only the lowest-index currently-untapped member is explorable
// (an earlier untapped member means ci is skipped; an earlier TAPPED member is on the DFS path and
// walked past). So any solution taps a PREFIX of each group.
//
// Realisation therefore needs no type-to-permanent search: sources are put in a canonical order
// (descriptor, then index), and the entry stores tap POSITIONS in that order rather than battlefield
// indices. Two boards sharing a canonical key have identical descriptor multisets, so position p names
// the same descriptor and the same rank within its group on both -- and rank-within-group ascending is
// exactly the prefix the DFS would have taken.
//
// HARD DEPENDENCY: this is only sound while the sibling collapse is ON. With MTG_NO_TAP_DUP_COLLAPSE=1
// the DFS may tap a non-prefix member, so canonical keying disables itself with it.
//
// *** SOUND NOW, BUT DEFAULT OFF: IT COSTS MORE THAN IT SAVES. ***
//
// ROOT CAUSE of the first version's divergence (found 2026-08-15): not the descriptor contents at all,
// but EXPLORATION ORDER. The DFS iterates candidates in battlefield-index order, so two boards with
// the same source MULTISET but a different interleaving explore differently and find different first
// solutions -- [Mountain@1, Island@2] tries the Mountain first, [Island@1, Mountain@2] tries the
// Island -- and for a cost either can pay, a tap-set cached from one is the wrong answer for the
// other. Rare by construction, which is why it showed as 6 diverged games in 1000 at treasure_hunt d0
// and passed a 1,300-game A/B on three other decks.
//
// The fix is to key on the ordered SEQUENCE of descriptors instead of the multiset: same sequence =>
// isomorphic search => same solution, expressed as ordinal positions in the source list. Absolute
// indices still drop out (which is the part that created spurious keys); the interleaving does not.
// Verified: treasure_hunt d0 seed 1001 divergences 6 -> 0, smoke 36/36 with 0 play-changed, and
// identical digests AND per-game logs across a 1,300-game six-config A/B.
//
// But the sequence key only cuts solve nodes -22% (6,584,731 -> 5,116,093), where the unsound multiset
// version cut 72%, and -22% of a ~10% slice does not pay for hashing a descriptor per source on every
// call. Best-of-two at 20 threads, indexed vs canonical: al_d5 -1.4%, fc_d3 +0.8%, ds_d3 +1.7%,
// ds_d5 +2.3%, fc_d5 +2.3%, al_d3 +4.5%. Consistently NEGATIVE.
//
// The 72% is still reachable, but only by canonicalising the DFS's OWN iteration order (sort candidates
// by descriptor rather than by index), which makes the multiset key sound. That changes which solution
// is found, so it is a play change needing a full A/B and a GT rebaseline -- not a byte-identical perf
// edit. Recorded in docs/design/fivecolour-mulligan-and-slow-atom.md section 5c.
//
// MTG_MANA_CACHE_CANON=1 enables the sound sequence version.
inline bool McCanonKey()
{
    static const bool v = EnvOn("MTG_MANA_CACHE_CANON");
    return v;
}
inline bool McSimpleTap(const CardDefinition* d)
{ return d && !d->params.storage_land && d->params.tap_opponent_lifegain == 0
           && !d->params.gy_land_exile_mana; }

// Scan the active player's mana sources ONCE: build the 128-bit key (k1,k2) AND the global gate.
// Returns false (caller skips the cache) if a state-dependent source is present.
inline bool ManaCacheKey(const GameState& state, const ManaCost& cost, bool for_creature,
                         std::uint64_t reserved_mask, int untapped_max,
                         bool want_leftover, bool want_full_pool, const ManaPool& floating,
                         std::uint64_t& k1, std::uint64_t& k2,
                         std::vector<std::uint64_t>* out_desc)
{
    auto mix = [](std::uint64_t& h, std::uint64_t v){ h ^= v; h *= 1099511628211ull; h ^= h >> 29; };
    std::uint64_t h1 = 1469598103934665603ull, h2 = 14695981039346656037ull;
    // Canonical mode collects one descriptor per source and hashes the SORTED multiset; indexed mode
    // hashes the sequence as it stands. `canon` holds (descriptor, index) so the sort is stable on
    // index -- which is what makes position p mean "rank within its group" and match the DFS's
    // ascending-index prefix rule.
    // Canonical mode combines the per-source descriptors COMMUTATIVELY (two independent 64-bit sums),
    // so the multiset is order-free with no sort -- a sort here cost more than the solves it saved on
    // low-redundancy boards (Dragonstorm measured 2-3% SLOWER). `out_desc` receives one descriptor per
    // battlefield slot so the store/hit paths can rank a source within its group by a single scan.
    const bool canon = (out_desc != nullptr);
    if (canon) { out_desc->assign(state.battlefield.size(), 0ull); }
    const int active = state.active_player_index;
    const int n = static_cast<int>(state.battlefield.size());
    int gy_fuel = -1;       // lazy, hashed once on the first Deathrite-style source
    int drip_useful = -1;   // lazy, hashed once on the first drip source
    bool domain_done = false;   // lazy, hashed once on the first domain source (Elder / Bloom Tender)
    bool scaled_done = false;   // lazy, hashed once on the first scaled source (Three Tree City)
    for (int i = 0; i < n; ++i)
    {
        const Permanent& p = state.battlefield[i];
        if (p.controller_index != active) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (!d) { continue; }
        if (!(d->tmpl == CardTemplate::BasicLand || d->tmpl == CardTemplate::ManaDork
              || d->params.mana_rock)) { continue; }
        // Domain source: its yield IS the colour domain -- the set of colours among every permanent
        // you control, most of which this loop never visits. Hash the realised SET (not a count: two
        // boards with two colours each pay different pips), once, exactly as `reflecting` does below.
        if ((McDomain(d) || McScaled(d)) && !McScalingHashed()) { return false; }   // legacy bail-out
        if (McDomain(d) && !domain_done)
        {
            domain_done = true;
            const std::vector<Color>& dom = DomainColors(state, active);
            mix(h1, 0xD0'A1'11ull); mix(h2, 0xD0'A1'22ull);
            for (Color c : dom)
            { mix(h1, static_cast<std::uint64_t>(c) + 1);
              mix(h2, (static_cast<std::uint64_t>(c) + 1) * 0xC2B2AE3D27D4EB4Full); }
        }
        // Scaled source: N = creatures you control, again mostly permanents this loop skips. The
        // feeder side (which sources can pay the {2}) is already keyed -- it reads only mana sources.
        if (McScaled(d) && !scaled_done)
        {
            scaled_done = true;
            const std::uint64_t nc = static_cast<std::uint64_t>(ScaledManaCreatureCount(state));
            mix(h1, 0x5CA1'ED'01ull + nc); mix(h2, 0x5CA1'ED'02ull ^ (nc << 24));
        }
        const std::uint64_t di = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(d));
        // In canonical mode every per-source field below goes into a DESCRIPTOR instead of straight
        // into the running hash, and the index is deliberately excluded (that is the whole point).
        // `dh` shadows the h1/h2 writes so the two modes stay one code path.
        std::uint64_t dh = 1469598103934665603ull;
        auto smix = [&](std::uint64_t a, std::uint64_t b)
        { if (canon) { mix(dh, a); } else { mix(h1, a); mix(h2, b); } };
        if (!canon) { mix(h1, static_cast<std::uint64_t>(i)); mix(h2, static_cast<std::uint64_t>(i) * 0x9E3779B97F4A7C15ull); }
        smix(di, di ^ 0xD1B54A32D192ED03ull);
        smix(p.tapped ? 1ull : 2ull, p.tapped ? 3ull : 5ull);
        // Chain-eligibility and the FULL counters vector -- the remaining two components of the
        // sibling collapse's equivalence. Counters matter because a tap DECREMENTS a depletion
        // counter: two Sandstone Needles on different counts are not interchangeable even though
        // production is counter-independent, and the indexed key never had to say so (same index =>
        // same permanent). Ineligible sources (tapped/reserved/sick dork) keep their own descriptor;
        // they are never tapped, so they only ever affect the key, never the tap-set.
        // CanTapNow is the single most expensive read in this loop, so compute it ONCE per source and
        // share it between the eligibility bit here and the dork-eligibility mix below. The first cut
        // called it twice per dork per key build, which is a large part of why canonical keying showed
        // no wall gain despite -22% solve nodes.
        int dork_elig = -1;   // -1 = not a dork / not needed
        if (canon)
        {
            if (!p.tapped && d->tmpl == CardTemplate::ManaDork)
            { dork_elig = CanTapNow(p, state.battlefield) ? 1 : 0; }
            const bool elig = !p.tapped && !((reserved_mask >> i) & 1ull) && dork_elig != 0;
            mix(dh, elig ? 0xE11Cull : 0xE11Dull);
            mix(dh, static_cast<std::uint64_t>(p.counters.size()));
            for (const Counter& ctr : p.counters)
            { mix(dh, static_cast<std::uint64_t>(ctr.type) * 1000003ull + static_cast<std::uint64_t>(ctr.count)); }
            mix(dh, static_cast<std::uint64_t>(p.storage_counters) * 2 + (p.storage_hold_this_turn ? 1ull : 0ull));
        }
        // Per-source DYNAMIC state the DFS reads that (i, def, tapped) does not capture. Each of
        // these was a latent stale-hit hole: two boards identical in (i, def, tapped) but differing
        // in one of the fields below solve DIFFERENTLY, so they must not share a key.
        if (!p.tapped)
        {
            // Dork tap-eligibility (summoning sickness / temp_haste / lord+equip haste): the worker's
            // is_src gates a ManaDork on CanTapNow, which an Expedite mid-turn or an untap-step
            // sickness flip changes without touching (def, tapped) -- the Mirrorwing case.
            if (d->tmpl == CardTemplate::ManaDork)
            {
                const bool elig = (dork_elig >= 0) ? (dork_elig != 0) : CanTapNow(p, state.battlefield);
                smix(elig ? 0xE119'01ull : 0xE119'02ull, elig ? 0xE119'03ull : 0xE119'05ull);
            }
            // Storage battery: burst amount = min(counters, shortfall) and StorageSourceLive gates on
            // counters>0 + the hold flag, so a charged vs uncharged (or held) Bazaar must key apart --
            // otherwise a stored negative goes stale when the battery charges.
            if (d->params.storage_land)
            {
                smix(0x570'1ull + static_cast<std::uint64_t>(p.storage_counters) * 2
                       + (p.storage_hold_this_turn ? 1ull : 0ull),
                     0x570'2ull ^ (static_cast<std::uint64_t>(p.storage_counters) << 8)
                       ^ (p.storage_hold_this_turn ? 0x100000ull : 0ull));
            }
        }
        // Deathrite (gy_land_exile_mana): production is gated on GraveyardLandFuel > 0, and each tap
        // consumes one fuel -- graveyard state the source loop cannot see. Hash the fuel count once.
        if (d->params.gy_land_exile_mana && gy_fuel < 0)
        {
            gy_fuel = GraveyardLandFuel(state, active);
            mix(h1, 0x6F'F1ull + static_cast<std::uint64_t>(gy_fuel));
            mix(h2, 0x6F'F2ull ^ (static_cast<std::uint64_t>(gy_fuel) << 16));
        }
        // Drip land (Grove of the Burnwillows): the worker's branch ORDER depends on the provider's
        // OpponentLifegainUseful(state) -- state outside (def, tapped) -- so the found solution (its
        // colour composition) can differ across boards that otherwise share a key. Hash the bit once.
        if (d->params.tap_opponent_lifegain > 0 && drip_useful < 0)
        {
            drip_useful = ResolveProvider(state).OpponentLifegainUseful(state, active) ? 1 : 0;
            mix(h1, drip_useful ? 0xD21'B1ull : 0xD21'B2ull);
            mix(h2, drip_useful ? 0xD21'B3ull : 0xD21'B5ull);
        }
        // Reflecting source (Reflecting Pool): its produces = union of the controller's OTHER lands'
        // colours (state-dependent). Hash that exact effective colour set so two boards where it can
        // make different colours get different keys -> byte-identical replay.
        if (d->params.reflecting)
        {
            const std::vector<Color>& rc = EffectiveProduces(state, active, *d);
            smix(0xEFEC71'11ull, 0xEFEC71'22ull);
            for (Color c : rc)
            { smix(static_cast<std::uint64_t>(c) + 1,
                   (static_cast<std::uint64_t>(c) + 1) * 0x9E3779B97F4A7C15ull); }
        }
        // ORDERED, not commutative -- and that is the whole correctness argument. The multiset version
        // was unsound: the DFS iterates candidates in BATTLEFIELD-INDEX order, so two boards with the
        // same source multiset but a different INTERLEAVING explore differently and find different
        // first solutions ([Mountain@1, Island@2] tries Mountain first; [Island@1, Mountain@2] tries
        // Island first), and a tap-set cached from one is simply the wrong answer for the other.
        // Hashing the SEQUENCE of descriptors keeps exploration order in the key while still dropping
        // the absolute indices, which is the part that was creating spurious keys.
        if (canon) { (*out_desc)[i] = dh; mix(h1, dh); mix(h2, dh * 0x9E3779B97F4A7C15ull); }
    }
    auto mixcost = [&](std::uint64_t& h){
        mix(h, static_cast<std::uint64_t>(cost.generic));
        mix(h, static_cast<std::uint64_t>(cost.white) | (static_cast<std::uint64_t>(cost.blue) << 16)
             | (static_cast<std::uint64_t>(cost.black) << 32) | (static_cast<std::uint64_t>(cost.red) << 48));
        mix(h, static_cast<std::uint64_t>(cost.green) | (static_cast<std::uint64_t>(cost.colorless) << 16));
        mix(h, static_cast<std::uint64_t>(cost.has_x ? 1 : 0) | (static_cast<std::uint64_t>(cost.x_pips) << 8)
             | (static_cast<std::uint64_t>(cost.hybrid_count) << 16));
        mix(h, static_cast<std::uint64_t>(cost.hybrid_pair[0]) | (static_cast<std::uint64_t>(cost.hybrid_pair[1]) << 8)
             | (static_cast<std::uint64_t>(cost.hybrid_pair[2]) << 16) | (static_cast<std::uint64_t>(cost.hybrid_pair[3]) << 24));
        mix(h, for_creature ? 1ull : 0ull);
        mix(h, reserved_mask);
        mix(h, static_cast<std::uint64_t>(static_cast<std::int64_t>(untapped_max)));
        // OUTPUT MODE is part of the SEARCH, not just of what gets returned: `collapse_colors` is
        // gated on out_leftover == nullptr, because the leftover pool is floated and later drained
        // colour-sensitively, so the recolour that mode allows would be observable. The two modes can
        // therefore find DIFFERENT first solutions for the same board and cost, and must key apart --
        // sharing one entry between them would hand a collapsed-colour pool to the caller that reads
        // colours. (want_full_pool rides along: it costs a bit and keeps the modes fully disjoint.)
        mix(h, want_leftover ? 0x1EF7ull : 0x1EF8ull);
        mix(h, want_full_pool ? 0xF0011ull : 0xF0012ull);
        // The INCOMING floating pool (a ritual's reserve on the filter-feed retry). Empty on the batch
        // path, so every pre-existing entry keys identically.
        mix(h, static_cast<std::uint64_t>(floating.white) | (static_cast<std::uint64_t>(floating.blue) << 12)
             | (static_cast<std::uint64_t>(floating.black) << 24) | (static_cast<std::uint64_t>(floating.red) << 36));
        mix(h, static_cast<std::uint64_t>(floating.green) | (static_cast<std::uint64_t>(floating.colorless) << 16)
             | (static_cast<std::uint64_t>(floating.wild) << 32));
    };
    mixcost(h1); mixcost(h2);
    k1 = h1; k2 = h2;
    return true;
}
} // namespace

// Public entry: thin wrapper over the worker. Identical behaviour; under MTG_TAP_STATS it records the
// payable/unpayable OUTCOME of each top-level call and the nodes it consumed (the recursion calls the
// worker directly, so every call here is exactly one top-level entry). Diagnostic only -- when the flag
// is off it is a straight forward, so zero cost.
bool TapForCostBacktrack(GameState& state, const ManaCost& cost,
                         bool for_creature, ManaPool floating,
                         const std::vector<Color>* rp_colors,
                         TapBacktrackMemo* fail_memo,
                         ManaPool* out_leftover,
                         std::uint64_t tapped_mask,
                         int untapped_max,
                         std::uint64_t reserved_mask,
                         ManaPool* out_full_pool,
                         const std::vector<std::pair<int, const CardDefinition*>>* src_cands)
{
    // ---- PAYABLE MANA CACHE lookup (canonical batch-prepay shape only; see the block above) ----
    // SHAPE: rp_colors must be null (externally-supplied colours are not in the key) and the board must
    // fit the tap-set bitmask. The output mode and the incoming floating pool are KEYED rather than
    // excluded (see McLeftoverShape), so both payment questions are cacheable; =0 restores the old
    // batch-prepay-only reach.
    const bool mc = ManaCacheEnabled() && rp_colors == nullptr && state.battlefield.size() <= 64
                    && (McLeftoverShape()
                        || (out_full_pool != nullptr && out_leftover == nullptr && floating.Total() == 0));
    std::uint64_t mk1 = 0, mk2 = 0, pre_tapped = 0; bool mc_active = false;
    // Pre-solve snapshots, taken on a MISS only, so the store below can recover the two things the
    // final state does not spell out: how many storage counters each tap burned, and how much drip the
    // solve applied. Both are DELTAS against these. thread_local so a miss allocates nothing after
    // warm-up; only charged batteries are recorded, so it is empty on every deck without one.
    static thread_local std::vector<std::pair<int, int>> mc_storage_pre;   // (index, counters before)
    int mc_opp_life_pre = 0;
    // Canonical order for this board (empty in indexed mode). Entry taps are POSITIONS in it.
    static thread_local std::vector<std::uint64_t> mc_desc;
    const bool mc_canon = McCanonKey();
    const bool mc_keyed = mc && ManaCacheKey(state, cost, for_creature, reserved_mask, untapped_max,
                                             out_leftover != nullptr, out_full_pool != nullptr, floating,
                                             mk1, mk2, mc_canon ? &mc_desc : nullptr);
    if (tapstats::Enabled() && !mc_keyed)
    { (mc ? tapstats::g_mc_skip_key : tapstats::g_mc_skip_shape).fetch_add(1, std::memory_order_relaxed); }
    if (mc_keyed)
    {
        auto it = g_mana_cache.find(mk1);
        if (it != g_mana_cache.end() && it->second.verify == mk2)
        {
            if (tapstats::Enabled()) { tapstats::g_mc_hit.fetch_add(1, std::memory_order_relaxed); }
            const ManaCacheEntry& e = it->second;
            if (!e.payable) { return false; }
            const int hit_active = state.active_player_index;
            for (const ManaCacheTap& t : e.taps)
            {
                // Canonical mode stores (descriptor, RANK); indexed mode stores the index itself. The
                // key pins the descriptor multiset, so the rank-th member of a descriptor group names
                // the same source on this board as on the one that filled the entry -- and ascending
                // rank is exactly the prefix the sibling collapse makes the DFS take.
                int idx = t.idx;
                if (mc_canon)
                {
                    idx = -1;
                    for (int q = 0, seen = 0; q < static_cast<int>(mc_desc.size()); ++q)
                    { if (mc_desc[q] != 0ull && seen++ == t.idx) { idx = q; break; } }
                    // The descriptor is re-checked, not just the position: a mismatch means the key
                    // let through two boards whose source lists are not actually isomorphic, which is
                    // a bug rather than a miss -- fail closed to the real solve instead of tapping the
                    // wrong permanent.
                    if (idx < 0 || mc_desc[idx] != t.desc) { idx = -1; }
                    if (idx < 0) { break; }
                }
                Permanent& p = state.battlefield[idx];
                p.tapped = true;
                const CardDefinition* td = CardDatabase::Instance().LookupCached(p.card);
                if (!td) { continue; }
                if (td->params.tap_self_damage > 0)   // City of Brass: replay the tap damage (life 1208-1209)
                { state.players[hit_active].life -= td->params.tap_self_damage; }
                // Depletion land (Sandstone Needle): replay the counter decrement the DFS's activate()
                // performs on the solution path. Relative (decrement whatever the current count is),
                // so the counter value never needs to be part of the key -- production is
                // counter-independent and a 0-count no-op replays as a no-op.
                if (td->params.enters_tapped_with_depletion > 0) { DecrementDepletionOnTap(p); }
                // Storage battery: the DFS burns only the PARTIAL shortfall, so the remainder persists.
                // Replayed as the recorded DELTA (not "set to X"), which keeps that partiality exact on
                // a board whose counter total differs -- and the key hashes the count, so it cannot.
                if (td->params.storage_land && t.storage_burn > 0) { p.storage_counters -= t.storage_burn; }
                // Deathrite Shaman: re-run the SHARED first-match exile rather than replaying a stored
                // index. The key hashes the graveyard-land COUNT, not its contents, so this board may
                // hold different lands than the one that filled the entry -- and running the picker is
                // precisely what the DFS would have done here. (Lockstep note: activate() keeps its own
                // inline copy of this scan because it also needs the slot for its undo; the two use the
                // same ZoneCard(...).IsLand() first-match predicate and must stay that way.)
                if (td->params.gy_land_exile_mana) { ExileGraveyardLandForMana(state, hit_active); }
            }
            // Drip (Grove of the Burnwillows): one aggregate call -- order-independent, and going
            // through OpponentGainsLife re-reads RemedyActive, so a Tainted Remedy board flips the
            // gain to a loss (and sets opponent_lost_life_this_turn) exactly as the real solve would.
            if (e.drip_life > 0) { OpponentGainsLife(state, hit_active, e.drip_life); }
            if (out_full_pool) { *out_full_pool = e.produced; }
            if (out_leftover)  { *out_leftover  = e.leftover; }
            return true;
        }
        if (tapstats::Enabled()) { tapstats::g_mc_miss.fetch_add(1, std::memory_order_relaxed); }
        mc_active = true;
        const int active = state.active_player_index;
        const int n = static_cast<int>(state.battlefield.size());
        mc_storage_pre.clear();
        mc_opp_life_pre = state.players[1 - active].life;
        for (int i = 0; i < n; ++i)
        { const Permanent& p = state.battlefield[i];
          if (p.controller_index != active) { continue; }
          if (p.tapped) { pre_tapped |= (1ull << i); }
          if (p.storage_counters > 0) { mc_storage_pre.push_back({ i, p.storage_counters }); } }
    }

    // ---- Run the real solve (preserving the MTG_TAP_STATS outcome accounting) ----
    bool ok;
    if (!tapstats::Enabled())
    {
        ok = TapForCostBacktrackWorker(state, cost, for_creature, floating, rp_colors, fail_memo,
                                       out_leftover, tapped_mask, untapped_max, reserved_mask,
                                       out_full_pool, src_cands);
    }
    else
    {
        const std::uint64_t nodes0 = tapstats::g_nodes.load(std::memory_order_relaxed);
        ok = TapForCostBacktrackWorker(state, cost, for_creature, floating, rp_colors, fail_memo,
                                       out_leftover, tapped_mask, untapped_max, reserved_mask,
                                       out_full_pool, src_cands);
        const std::uint64_t dn = tapstats::g_nodes.load(std::memory_order_relaxed) - nodes0;
        if (ok) { tapstats::g_entries_ok.fetch_add(1, std::memory_order_relaxed);
                  tapstats::g_nodes_ok.fetch_add(dn, std::memory_order_relaxed); }
        else    { tapstats::g_entries_fail.fetch_add(1, std::memory_order_relaxed);
                  tapstats::g_nodes_fail.fetch_add(dn, std::memory_order_relaxed); }
        // Same nodes, split by the cache's REACH instead of by outcome: a hit costs no nodes, so
        // everything here is either a miss (cacheable, not yet memoised) or a shape the cache skips.
        (mc_active ? tapstats::g_mc_nodes_miss : tapstats::g_mc_nodes_skip)
            .fetch_add(dn, std::memory_order_relaxed);
    }

    // ---- Store (negatives always; a positive records its tap-set + the two order-dependent deltas) --
    if (mc_active)
    {
        if (g_mana_cache.size() > 500000) { g_mana_cache.clear(); }   // bound cross-rollout growth
        ManaCacheEntry e; e.verify = mk2; e.payable = ok; e.drip_life = 0; bool storable = true;
        if (ok)
        {
            if (out_full_pool) { e.produced = *out_full_pool; }
            if (out_leftover)  { e.leftover = *out_leftover; }
            const int active = state.active_player_index;
            const int n = static_cast<int>(state.battlefield.size());
            // Only drip moves the OPPONENT's life during a payment (tap_self_damage hits the active
            // player), so the magnitude of that one diff is the total drip the solve applied -- and
            // magnitude is all the replay needs, since OpponentGainsLife re-derives the sign.
            e.drip_life = std::abs(state.players[1 - active].life - mc_opp_life_pre);
            for (int i = 0; i < n; ++i)
            {
                const Permanent& p = state.battlefield[i];
                if (p.controller_index != active) { continue; }
                if (p.tapped && !((pre_tapped >> i) & 1))   // newly tapped by this solve
                {
                    const CardDefinition* pd = CardDatabase::Instance().LookupCached(p.card);
                    if (!McAllSources() && !McSimpleTap(pd)) { storable = false; break; }
                    int burn = 0;
                    if (pd && pd->params.storage_land)
                    {
                        for (const std::pair<int, int>& s : mc_storage_pre)
                        { if (s.first == i) { burn = s.second - p.storage_counters; break; } }
                    }
                    std::uint64_t desc = 0; int slot = i;
                    if (mc_canon)
                    {
                        desc = (i < static_cast<int>(mc_desc.size())) ? mc_desc[i] : 0ull;
                        if (desc == 0ull) { storable = false; break; }   // tapped a source the key never saw
                        slot = 0;                                        // ORDINAL position in the source list
                        for (int q = 0; q < i; ++q) { if (mc_desc[q] != 0ull) { ++slot; } }
                    }
                    e.taps.push_back({ desc, slot, burn });
                }
            }
        }
        if (tapstats::Enabled() && !storable)
        { tapstats::g_mc_unstorable.fetch_add(1, std::memory_order_relaxed); }
        if (storable) { g_mana_cache[mk1] = std::move(e); }
    }
    return ok;
}

// Format the one aggregated life-watcher event for the play viewer (viewer issue #11). See the
// declaration in SpellEffects.h for why this is out-of-line: it runs only on a real human-play
// resolution (the event sink is nulled in every search/rollout), so it is cold by construction.
//
// `subject_index >= 0` names the creature that ENTERED (the enter-watcher case); -1 means the
// subject is a creature that DIED (the Massacre Wurm case -- the dead card has already left the
// battlefield, so there is no index left to name it by). Both read the same way in the history:
// what happened, which watchers saw it (with multiplicity), and the resulting life swing.
//
//   "<drop> 1/1 Spirit Token entered under the opponent -- Suture Priest x2: opponent -2 (12->10)"
//   "<drop> Birds of Paradise entered -- Essence Warden: you +1 (20->21)"
std::string DescribeLifeWatchers(const GameState& state, int subject_controller, int subject_index,
                                 const std::vector<std::pair<std::string, int>>& on_subject,
                                 const std::vector<std::pair<std::string, int>>& on_other,
                                 int life_before_subject, int life_before_other)
{
    auto who = [&](int idx) { return idx == state.active_player_index ? "you" : "opponent"; };
    auto list = [](const std::vector<std::pair<std::string, int>>& v)
    {
        std::string s;
        for (const auto& e : v)
        {
            if (!s.empty()) { s += ", "; }
            s += e.first;
            if (e.second > 1) { s += " \xC3\x97" + std::to_string(e.second); }   // " x2"
        }
        return s;
    };
    auto swing = [&](int idx, int before)
    {
        const int now = state.players[idx].life, d = now - before;
        return std::string(who(idx)) + " " + (d >= 0 ? "+" : "\xE2\x88\x92")   // U+2212 MINUS SIGN
             + std::to_string(std::abs(d)) + " (" + std::to_string(before)
             + "\xE2\x86\x92" + std::to_string(now) + ")";                     // U+2192 arrow
    };

    std::string head;
    if (subject_index >= 0 && subject_index < static_cast<int>(state.battlefield.size()))
    {
        head = state.battlefield[subject_index].card.m_name.str() + " entered";
        if (subject_controller != state.active_player_index) { head += " under the opponent"; }
    }
    else
    {
        head = std::string("a creature the ") + who(subject_controller) + " controlled died";
    }

    // One clause per side whose life actually moved, subject side first (that is the side the
    // event is ABOUT). A side with no watcher contributes nothing rather than a "+0".
    std::string body;
    if (!on_subject.empty())
    { body += list(on_subject) + ": " + swing(subject_controller, life_before_subject); }
    if (!on_other.empty())
    {
        if (!body.empty()) { body += "; "; }
        body += list(on_other) + ": " + swing(1 - subject_controller, life_before_other);
    }
    // Icon by what actually happened to the OPPONENT: a drain reads as blood, a pure lifegain
    // (Essence Warden / Suture Priest clause 1 off our own creature) reads as a heart -- matching
    // the lifegain/lifeloss split the ApplyOpponentLifegain event already uses.
    const int opp = 1 - state.active_player_index;
    const int opp_before = (opp == subject_controller) ? life_before_subject : life_before_other;
    const bool drained = state.players[opp].life < opp_before;
    return (drained ? "\xF0\x9F\xA9\xB8 " : "\xF0\x9F\x92\x9A ")   // U+1FA78 blood / U+1F49A heart
         + head + " \xE2\x80\x94 " + body;                          // em dash
}
