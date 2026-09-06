// Unified land drop -- see LandPlay.h for why this exists and what each caller passes.
#include "../core/EnvFlags.h"
#include "LandPlay.h"
#include "../cards/CardDatabase.h"
#include "../core/GameLogger.h"
#include "../core/RolloutTouch.h"
#include "../core/SpellEffects.h"
#include "EngineFlags.h"
#include <algorithm>

bool PlayLandFromHand(GameState& state, std::size_t hand_index, const CardDefinition& def,
                      const LandPlayOptions& opts)
{
    Player& ap = state.ActivePlayer();
    if (hand_index >= ap.hand.size()) { return false; }
    auto it = ap.hand.begin() + static_cast<std::ptrdiff_t>(hand_index);

    if (opts.record_touch) { rollout_touch::Record(it->m_name.str()); }
    if (opts.logger)       { opts.logger->LogPlayLand(it->m_number, it->m_name); }

    // Fetchland: the land drop sacrifices the fetchland to search out a real land. fetch_target
    // names the searched choice; empty -> PerformFetch falls back to its own top heuristic pick.
    if (!def.params.fetch_land_types.empty())
    {
        Card fetchland = *it;
        ap.hand.erase(it);
        ++ap.lands_played_this_turn;
        ap.graveyard.push_back(fetchland);
        PerformFetch(state, state.active_player_index, def.params, opts.fetch_target);
        return true;
    }

    // Modal double-faced land (Pathway): the back face is a distinct single-colour land identity
    // synthesized in the DB (mdfc_back_*); entering the permanent AS that identity locks its
    // colour, which every mana site reads live off the permanent's name. The faces share all OTHER
    // characteristics, so the tapped / ETB logic below reads the front `def` unchanged.
    const CardDefinition* face_def = &def;
    // A spell//land MDFC (nonland front, e.g. Turntimber Symbiosis) has exactly ONE land face --
    // the back -- so a land play of it ALWAYS resolves as the back, whatever face the caller
    // passed. A land//land Pathway keeps the explicit face choice.
    if ((opts.land_face == "back" || !def.card.IsLand()) && !def.params.mdfc_back_name.empty())
    {
        const CardDefinition* bd = CardDatabase::Instance().Lookup(def.params.mdfc_back_name);
        if (bd) { face_def = bd; }
    }
    // Every "as this land enters" decision below reads the FACE definition. For a land//land
    // Pathway the faces share these params (back synthesized from the front), so `fdef` == the
    // old front read -- byte-identical; for a spell//land only the back carries land semantics
    // (Turntimber: pay 3 life or enter tapped).
    const CardDefinition& fdef = *face_def;

    // Resolve "as this land enters" choices (shock life payment, reveal-untap) while the card is
    // still in hand; this also tells us whether it enters tapped.
    bool tapped;
    if (opts.honor_entry_chooser && g_play_land_entry_chooser && LandEntryHasChoice(state, fdef))
    {
        bool heur_untapped = !LandWouldEnterTapped(state, fdef, opts.allow_shock_pay);
        bool untapped = (*g_play_land_entry_chooser)(
            state, state.active_player_index, fdef.card.m_name.str(),
            fdef.params.etb_pay_life_to_untap,
            fdef.params.etb_untap_reveal_subtypes, heur_untapped);
        if (untapped) { ApplyLandUntapPayment(state, fdef); }
        tapped = !untapped;
    }
    else
    {
        tapped = LandEntersTapped(state, fdef, opts.allow_shock_pay);
    }

    // "You may have this land enter tapped. If you do, you get N rad counters." Resolved here, after
    // the other entry choices, because it can only ever ADD tapped-ness: declining leaves whatever
    // the clauses above decided, so a land that already enters tapped for another reason is
    // unaffected. -1 (no searched variant, no human pick) keeps the pre-2026-09-02 behaviour exactly
    // -- decline -- so every deck without such a land is byte-identical.
    int rad_gain = 0;
    if (fdef.params.etb_optional_tapped_rad > 0)
    {
        bool take = false;
        // Gated on the chooser POINTER alone, not on honor_entry_chooser: that flag belongs to the
        // shock/reveal axis, and this must be askable on the executor's real drop without turning
        // that one on too. The pointer is nulled by RevealLogPause for every search/rollout scope,
        // so the search still scores Plan::rad_mode and autonomous play is untouched.
        if (g_play_land_rad_chooser != nullptr)
        {
            take = (*g_play_land_rad_chooser)(state, state.active_player_index,
                                              fdef.card.m_name.str(),
                                              fdef.params.etb_optional_tapped_rad);
        }
        else if (opts.rad_mode >= 0) { take = (opts.rad_mode != 0); }
        if (take) { tapped = true; rad_gain = fdef.params.etb_optional_tapped_rad; }
    }

    Permanent perm;
    perm.card              = face_def->card;   // chosen face's identity -> locks its colour
    // Preserve the per-copy ID from the hand card: BounceKarooLand returns `battlefield[i].card` to
    // hand, so an unnumbered land permanent hands the bounce an unnumbered card and any decision
    // keyed on card identity diverges (Hinata seed 4153 T3, when the rollout twin lacked this).
    perm.card.m_number     = it->m_number;
    perm.controller_index  = state.active_player_index;
    perm.owner_index       = state.active_player_index;
    perm.entered_this_turn = true;
    perm.tapped            = tapped;
    if (fdef.params.enters_tapped_with_depletion > 0)
    {
        Counter dep;
        dep.type  = Counter::Type::Depletion;
        dep.count = fdef.params.enters_tapped_with_depletion;
        perm.counters.push_back(dep);
    }
    state.battlefield.push_back(perm);

    ap.hand.erase(it);
    ++ap.lands_played_this_turn;
    // Counters are gained as the land enters, so AFTER it is on the battlefield -- and before any
    // ETB below can read them.
    if (rad_gain > 0) { ap.rad_counters += rad_gain; }
    // "When this land enters, you get {E}" (Aether Hub). A PLAYER resource, like rad counters, and
    // gained here for the same reason: as the land enters, before any ETB below can read it. This
    // is THE land drop for all three callers (the executor's searched drop, its greedy drop, and
    // the rollout), so one edit covers both worlds.
    if (fdef.params.etb_energy > 0) { ap.energy_counters += fdef.params.etb_energy; }

    // ETB effects, after the land is on the battlefield (face definition; see fdef above).
    if (fdef.params.etb_scry > 0)
    {
        if (opts.label_look_source) { ScryTop(state, fdef.params.etb_scry, fdef.card.m_name); }
        else                        { ScryTop(state, fdef.params.etb_scry); }
    }
    if (fdef.params.etb_surveil > 0)
    {
        if (opts.label_look_source) { SurveilTop(state, fdef.params.etb_surveil, fdef.card.m_name); }
        else                        { SurveilTop(state, fdef.params.etb_surveil); }
    }
    if (fdef.params.etb_bounce_land)
    {
        BounceKarooLand(state, state.active_player_index,
                        static_cast<int>(state.battlefield.size()) - 1);
    }
    // "When this land enters, you gain N life" (Kazandu Refuge). Plain controller lifegain --
    // no gain-reversal enabler interaction (OpponentGainsLife is for the OPPONENT's gain).
    if (fdef.params.etb_lifegain > 0)
    {
        ap.life += fdef.params.etb_lifegain;
        ap.life_gained_this_turn += fdef.params.etb_lifegain;   // Kazandu Refuge feeds Fortifying Draught's X
    }
    // Forbidden Orchard played this turn: it is tapped for mana this turn too, so spawn the
    // opponent's Spirit now (the turn-start spawn only covers copies already in play).
    if (opts.spawn_orchard_spirit && IsForbiddenOrchard(&fdef)) { SpawnOpponentSpirit(state); }
    // Snow-enter scry watcher (Marit Lage's Slumber): land drops do NOT route through
    // FireEtbWatchers, and most of the Snow deck's snow permanents are lands -- so the narrow,
    // param-gated watcher is hooked here too. Deliberately NOT a blanket FireEtbWatchers call
    // (that would change every deck's enter cascade); one supertype bit-test for non-snow decks.
    // This is THE land drop for all three callers, so this one edit is executor/rollout lockstep.
    // Re-find the just-played land by number: the ETBs above can reshape the battlefield (a Karoo
    // bounce erases an element, an Orchard spawn appends one), so "last element" is not it.
    for (int li = static_cast<int>(state.battlefield.size()) - 1; li >= 0; --li)
    {
        const Permanent& lp = state.battlefield[li];
        if (!lp.is_token && lp.card.m_number == perm.card.m_number
            && lp.controller_index == state.active_player_index)
        { FireSnowEnterWatchers(state, li); break; }
    }
    return true;
}

// Would the extra mana from an UNTAPPED first land sit idle this turn? See the call site in
// GreedyLandChoiceIndex for the rationale; this is the safety envelope, and every clause below is
// there to make a false TRUE impossible rather than to widen the rule's reach.
//
// SCOPED TO THE FIRST LAND DROP OF THE GAME (the active player controls no land). That is the case
// the USER ruled on, it is where the census says the damage is, and -- decisively -- it is the only
// state in which "nothing can consume the mana" is CHEAP TO PROVE. With a board this bare the whole
// space of mana sinks is the hand, so a scan of hand costs is exhaustive. One turn later it is not:
// a permanent's activated ability is a sink too, and the engine has 30-odd separate optional<ManaCost>
// params for those with no generic "has an activation" probe. Enumerating them here would be a
// hand-maintained list that silently rots every time a card shape is added -- the same failure mode
// the reference-deck map just had to be rescued from -- and the rot would be INVISIBLE, because a
// missed sink makes the rule fire when it should not and merely looks like a slightly worse land
// drop. Widening past turn one needs that probe to exist first; it is a follow-up with its own
// measurement, not a constant to relax.
static bool FirstDropManaWouldSitIdle(const GameState& state, bool hold_top_consumer)
{
    const Player& ap = state.ActivePlayer();

    for (const Permanent& p : state.battlefield)
    {
        // Any permanent at all disqualifies: a land is a mana source that changes the arithmetic,
        // and a nonland is a potential activated-ability sink (see the header).
        if (p.controller_index == state.active_player_index) { return false; }
    }

    // Both kinds must be on offer, or there is no order to invert. `untapped_net` is the most mana
    // any DEFERRABLE untapped candidate could add -- taking the max is the conservative side: if
    // even the best of them cannot reach the cheapest action, none of them can.
    bool has_tapped   = false;
    int  untapped_net = -1;
    for (const Card& c : ap.hand)
    {
        if (c.m_impulse_no_land) { continue; }
        const CardDefinition* front = CardDatabase::Instance().LookupCached(c);
        const CardDefinition* def   = LandFaceDefOf(front);
        if (!def) { continue; }
        if (hold_top_consumer && front != nullptr
            && front->params.look_top_put_creature_count > 0) { continue; }
        // A Karoo needs another land to bounce and there is none (this is the first drop), so it is
        // not a candidate here at all -- matching the four-pass scan's own skip.
        if (def->params.etb_bounce_land) { continue; }
        const bool is_tapped = LegacyStaticTapped() ? def->params.enters_tapped
                                                    : LandWouldEnterTapped(state, *def);
        if (is_tapped) { has_tapped = true; continue; }
        // DEFERRING MUST KEEP THE UNTAPPED-NESS, or the trade this rule is built on does not hold.
        // A fastland enters untapped only while few other lands are out, and a reveal-untap land
        // reads a hand that will have changed by the time we come back to it: for those the untapped
        // drop really is use-it-or-lose-it, so one in hand keeps the whole untapped-first order.
        // (A shock land is fine -- its life payment is just as available next turn.)
        if (def->params.fastland_max_other_lands >= 0)      { return false; }
        if (!def->params.etb_untap_reveal_subtypes.empty()) { return false; }
        untapped_net = std::max(untapped_net, SourceMaxNet(*def));
    }
    if (!has_tapped || untapped_net <= 0) { return false; }

    // The cheapest mana outlay the turn has available. Compared by MANA VALUE, not colour, and that
    // is the conservative direction: a card affordable by value but uncastable by colour makes this
    // read TOO SMALL, which makes the rule DECLINE to fire. It can never manufacture a fire.
    int cheapest = -1;
    const auto note = [&cheapest](int mv) { if (cheapest < 0 || mv < cheapest) { cheapest = mv; } };
    for (const Card& c : ap.hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (!d) { continue; }
        if (!d->card.IsLand()) { note(d->card.m_mana_cost.ManaValue()); }
        // Cycling is a real mana sink and the reason this must not just read mana values: on this
        // deck a Fluctuator-discounted cycle costs {0}, and a turn that can cycle is a turn whose
        // mana is NOT idle. EffectiveCyclingCostFor carries the discount (SpellEffects.h).
        if (d->params.cycling_cost.has_value())
        { note(EffectiveCyclingCostFor(state, *d).ManaValue()); }
        if (d->params.sacrifice_draw_cost.has_value())
        { note(d->params.sacrifice_draw_cost->ManaValue()); }
    }
    if (cheapest < 0) { return true; }        // nothing but lands in hand: nothing to spend on

    // Idle exactly when the untapped drop still cannot buy the cheapest thing available.
    return untapped_net < cheapest;
}

int GreedyLandChoiceIndex(const GameState& state)
{
    const Player& ap = state.ActivePlayer();
    const int n = static_cast<int>(ap.hand.size());

    // Pre-pass: prioritize a no_max_hand_size land (Reliquary Tower) when either a DrawUntilNonland
    // spell (Treasure Hunt) is in hand (play it BEFORE the draw) OR the hand is already flooding past
    // max size (play it AFTER a draw to KEEP the cards). The latter is the gi=65/gi=881 case:
    // Treasure Hunt resolved and drew a Reliquary, but with TH no longer in hand the old
    // TH-in-hand-only check missed it, so the drawn Reliquary (and the whole flood) was discarded at
    // cleanup instead of kept as Land's Edge ammo.
    bool has_draw_until_nonland = false;
    for (const Card& c : ap.hand)
    {
        const CardDefinition* cdef = CardDatabase::Instance().LookupCached(c);
        if (cdef && cdef->tmpl == CardTemplate::DrawUntilNonland) { has_draw_until_nonland = true; break; }
    }
    const bool hand_flooding = n > 7;
    if (has_draw_until_nonland || hand_flooding)
    {
        for (int i = 0; i < n; ++i)
        {
            if (ap.hand[i].m_impulse_no_land) { continue; }   // Apex-exiled land: never played
            const CardDefinition* def = CardDatabase::Instance().LookupCached(ap.hand[i]);
            if (!def || !def->card.IsLand() || !def->params.no_max_hand_size) { continue; }
            return i;
        }
    }

    // A Karoo bounce land with no other land in play must return ITSELF (the bounce is mandatory) --
    // net no land in play and a wasted drop, never the right play. Skip it below until another land
    // is down (matches SimulateLandPlay and the searched land drop).
    bool has_other_land = false;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index == state.active_player_index && p.card.IsLand())
        { has_other_land = true; break; }
    }

    // Tutor-top combo hold (TopResolveEnabled -- the USER's reset, 2026-08-21). A spell//land
    // top-consumer (Turntimber Symbiosis) in hand is the combo's consumer half: tutor_to_top
    // stacks a creature, the consumer puts it onto the battlefield. The greedy drop runs BEFORE
    // subset scoring at depth 0, so playing the consumer as the LAND burns the piece before
    // Solve/ExtraLethalDamage can ever value the line (the isolated fixture measured exactly
    // this). When the line is live -- a tutor_to_top in hand, a team-pump threat still in the
    // library, and the pre-drop pool already affording tutor + consumer -- skip look-top cards
    // in the scan: another land plays instead, or no land at all (the combo outvalues the drop).
    // Gated on the reset lever + both params in hand -> every other deck/state byte-identical.
    bool hold_top_consumer = false;
    if (TopResolveEnabled())
    {
        int wt_mv = -1, tt_mv = -1;
        for (const Card& c : ap.hand)
        {
            const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
            if (!d) { continue; }
            if (d->params.tutor_to_top) { wt_mv = d->card.m_mana_cost.ManaValue(); }
            if (d->params.look_top_put_creature_count > 0)
            {
                const int mv = d->card.m_mana_cost.ManaValue();
                if (tt_mv < 0 || mv < tt_mv) { tt_mv = mv; }
            }
        }
        if (wt_mv >= 0 && tt_mv >= 0)
        {
            bool hoof_in_lib = false;
            for (const Card& c : ap.library)
            {
                const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
                if (d && d->params.etb_team_pump_per_creature) { hoof_in_lib = true; break; }
            }
            if (hoof_in_lib && AvailableManaPool(state, nullptr).Total() >= wt_mv + tt_mv)
            { hold_top_consumer = true; }
        }
    }

    // "PLAY THE TAPPED LAND WHILE THE MANA WOULD SIT IDLE" (MTG_LAND_IDLE_TAPPED_FIRST, default
    // OFF -- measuring). USER ruling, 2026-09-05, shown the turn-one census: "Wow, that is
    // surprising on the untapped land T1. That is never a good idea."
    //
    // The four passes below prefer an untapped land UNCONDITIONALLY, and that preference is only
    // ever right when the mana can be SPENT this turn. When it cannot, it is strictly losing:
    //
    //     untapped now, tapped next  ->  1 usable this turn, 1 usable next turn
    //     tapped now, untapped next  ->  0 usable this turn, 2 usable next turn
    //
    // Nothing consumes the 1 in line one, so line two dominates it -- deferring the untapped land
    // costs nothing now and buys a full extra mana on every later turn. This is the ordinary
    // manabase-sequencing principle, not a deck quirk, which is why it is built here in the shared
    // ranker rather than in a provider.
    //
    // WHY IT MATTERS HERE. On Fluctuator 38 of 42 lands enter tapped, so two mana on turn two IS
    // the deck (untapped T2 drop: 3.554 avg / 58% T3 wins; tapped: 4.306 / 6.5%), and the ranker
    // spent the untapped land on turn one in ~42% of the games where it had the choice. That was
    // INVARIANT across d3 and d5 -- not because the search is blind to the trade, but because it
    // rates the two lines equal at the horizon and falls through to this ranker as its last-resort
    // plan-ordering tiebreak (see greedy_land_name in TurnSolver's EnumeratePlansWithLandUncached).
    // Fixing the ranker therefore reaches all three land-drop sites at once.
    const bool idle_first_drop = LandIdleTappedFirstEnabled()
                              && FirstDropManaWouldSitIdle(state, hold_top_consumer);

    // Four-pass priority: 0 = untapped+multi, 1 = untapped+any, 2 = tapped+multi, 3 = tapped+any.
    // Under the rule above the two halves swap -- tapped+multi, tapped+any, untapped+multi,
    // untapped+any -- so the multi-colour preference inside each half is preserved exactly.
    for (int pass = 0; pass < 4; ++pass)
    {
        const bool want_untapped = idle_first_drop ? (pass >= 2) : (pass < 2);
        const bool want_multi    = (pass == 0 || pass == 2);
        // Closing-window sub-order: a fastland enters untapped ONLY while few other lands are out, so
        // its untapped drop is use-it-or-lose-it while an always-untapped land is as good later. It
        // lives INSIDE the pass on purpose, so it only ever breaks a tie among otherwise-equal
        // options and can never outrank an untapped drop or the multi-colour preference.
        for (int sub = 0; sub < 2; ++sub)
        {
        if (sub == 0 && !LandClosingWindowEnabled()) { continue; }   // rule off -> single unfiltered scan
        for (int i = 0; i < n; ++i)
        {
            if (ap.hand[i].m_impulse_no_land) { continue; }
            const CardDefinition* front = CardDatabase::Instance().LookupCached(ap.hand[i]);
            const CardDefinition* def   = LandFaceDefOf(front);   // spell//land plays its back face
            if (!def) { continue; }
            if (hold_top_consumer && front != nullptr
                && front->params.look_top_put_creature_count > 0) { continue; }   // combo piece, not the drop
            if (def->params.etb_bounce_land && !has_other_land) { continue; }
            if (LandClosingWindowEnabled())
            {
                const bool closing = def->params.fastland_max_other_lands >= 0
                                  && !LandWouldEnterTapped(state, *def);   // window still open
                if ((sub == 0) != closing) { continue; }
            }
            // Tapped-ness must be the DYNAMIC answer, not the static flag: a fastland and a
            // shock/reveal land all carry enters_tapped == false yet enter TAPPED depending on
            // board/life/hand. LandWouldEnterTapped is the pure predicate; LandEntersTapped must NOT
            // be used here, it PAYS the shock life as a side effect.
            const bool is_tapped = LegacyStaticTapped() ? def->params.enters_tapped
                                                        : LandWouldEnterTapped(state, *def);
            // NOT UnconditionalProduces here, and the measurement is why. Semantically an
            // any-colour filter is not a fixing land on its own -- but this test feeds the
            // greedy's multi-colour PASS, and from turn two on Capital City really is the best
            // fixing land the deck has (any second land feeds it). Demoting it out of pass 0
            // measured d0 +0.0220 on both tiers, 6 games slower / 1 faster, so `produces` is the
            // better answer here even though it is the looser one. The honest predicate would be
            // board-dependent (has it got a feeder RIGHT NOW), which a static def-only test
            // cannot express -- left to the search, which reads the real board.
            const bool is_multi  = def->params.produces.size() > 1;
            if (want_untapped && is_tapped)   { continue; }
            if (!want_untapped && !is_tapped) { continue; }
            if (want_multi && !is_multi)      { continue; }
            return i;
        }
        }
    }
    return -1;
}
