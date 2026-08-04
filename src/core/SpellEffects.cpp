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
    std::string want = target_name;
    if (want.empty())
    {
        std::vector<std::string> cands = ResolveProvider(state).TutorCandidates(state, controller_index, pp);
        if (cands.empty()) { return; }
        want = cands.front();
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
            // first matching library card anyway. Dedup preserves first-occurrence order, so index 0
            // is still cands.front() -- the heuristic default the viewer pre-selects.
            std::vector<std::string> uniq;
            for (const std::string& c : cands)
            { if (std::find(uniq.begin(), uniq.end(), c) == uniq.end()) { uniq.push_back(c); } }
            const int picked = (*g_play_tutor_chooser)(state, controller_index, source_name, uniq, 0);
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
        EmitReveal(state.turn_number, source_name + " (searched)",
                   { fetched_num }, { fetched_name }, { fetched_num }, {},
                   /*dispositions*/ { "to hand" });
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

bool TapForCostBacktrack(GameState& state, const ManaCost& cost,
                                bool for_creature, ManaPool floating,
                                const std::vector<Color>* rp_colors,
                                TapBacktrackMemo* fail_memo,
                                ManaPool* out_leftover,
                                std::uint64_t tapped_mask,
                                int untapped_max,
                                std::uint64_t reserved_mask,
                                ManaPool* out_full_pool)
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
                if (!StorageSourceLive(p, *d)) { continue; }   // uncharged storage land makes no mana
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
        if (!StorageSourceLive(state.battlefield[i], *def)) { continue; }   // uncharged storage: no mana

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
            // Grove of the Burnwillows drip (opponent gains -> loses with Remedy). Restored
            // below on failure alongside the active player's life.
            if (drip_ok && def->params.tap_opponent_lifegain > 0)
            { OpponentGainsLife(state, active, def->params.tap_opponent_lifegain); }
            if (TapForCostBacktrack(state, cost, for_creature, next, rp_colors, fail_memo, out_leftover,
                                    tapped_mask | (1ull << i),
                                    untapped_max < 0 ? -1 : untapped_max - src_max_net,
                                    reserved_mask, out_full_pool)) { return true; }
            state.battlefield[i].tapped = tapped_snap;   // only this source was touched at this level
            if (has_counters) { state.battlefield[i].counters = counters_snap; }
            state.battlefield[i].storage_counters = storage_snap;
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
                for (Color c : produces)
                { CcoAuditTap(*def, c, for_creature);   // audit: `produces` is cco-stripped above
                  ManaPool f = floating; f.Add(c, amt); if (activate(f, /*drip_ok=*/true, storage_burn)) { return true; } }
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
