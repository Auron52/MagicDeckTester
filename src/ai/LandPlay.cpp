// Unified land drop -- see LandPlay.h for why this exists and what each caller passes.
#include "../core/EnvFlags.h"
#include "LandPlay.h"
#include "../cards/CardDatabase.h"
#include "../core/GameLogger.h"
#include "../core/RolloutTouch.h"
#include "../core/SpellEffects.h"
#include "EngineFlags.h"
#include "DecisionProviders.h"
#include <algorithm>

// THE BODY OF THE LAND DROP, shared by both source zones (2026-10-15, Courser of Kruphix). `card` is
// the card being played; `leave_zone` removes it from its source zone and is called at EXACTLY the
// points the hand drop always erased its card (before the fetchland's graveyard push; after the
// permanent is on the battlefield), so the hand path is byte-identical to the body it was factored
// out of. `card` must not be read after `leave_zone` runs (for the hand it is a reference into the
// hand). The library-top path has already popped its card, so its `leave_zone` is a no-op -- and
// popping FIRST is the rule-correct order there: the card leaves the library as it is played, so a
// land ETB that reads the library (a scry) sees the new top, never the land itself. Templated rather
// than std::function: this runs on every simulated land drop.
template <class LeaveZone>
static bool PlayLandCore(GameState& state, const Card& card, const CardDefinition& def,
                         const LandPlayOptions& opts, LeaveZone&& leave_zone)
{
    Player& ap = state.ActivePlayer();

    if (opts.record_touch) { rollout_touch::Record(card.m_name.str()); }
    if (opts.logger)       { opts.logger->LogPlayLand(card.m_number, card.m_name, opts.from_library_top); }

    // Fetchland: the land drop sacrifices the fetchland to search out a real land. fetch_target
    // names the searched choice; empty -> PerformFetch falls back to its own top heuristic pick.
    if (!def.params.fetch_land_types.empty())
    {
        Card fetchland = card;
        leave_zone();
        ++ap.lands_played_this_turn;
        ap.graveyard.push_back(fetchland);
        // LANDFALL for the FETCHLAND ITSELF (route 1 of OnLandEntered's contract). The engine
        // collapses the fetchland's battlefield stay to nothing (it goes straight to the graveyard
        // above), but the real card is PLAYED -- it enters the battlefield and is only then
        // sacrificed -- so its entry triggers landfall, and the land it fetches triggers again
        // through PerformFetch -> EnterLand (route 2): two triggers, as in a real game. Fired before
        // the fetch because that is the order the two entries happen in. Gated per game inside, so
        // every deck without a landfall card is byte-identical.
        OnLandEntered(state, state.active_player_index, fetchland.m_number);
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
    perm.card.m_number     = card.m_number;
    perm.controller_index  = state.active_player_index;
    perm.owner_index       = state.active_player_index;
    perm.entered_this_turn = true;
    perm.tapped            = tapped;
    // Routed through the shared chokepoint so Doubling Season applies (CR 121.6 / 614.1c -- the
    // same rule that doubles devour's enters-with counters). Stamping the Counter here directly is
    // what made depletion the one counter kind a doubler could not see; see PutDepletionCounters.
    PutDepletionCounters(state, perm, fdef.params.enters_tapped_with_depletion);
    state.battlefield.push_back(perm);
    // ASCEND (Ocelot Pride): a land drop raises the permanent count, and land drops deliberately do
    // NOT route through FireEtbWatchers (see the snow-watcher note below), so the city's-blessing
    // state trigger is checked here too. Placed on the RISE -- before the ETBs below -- so a Karoo
    // that enters as the tenth permanent and then bounces a land still grants the blessing. This is
    // THE land drop for all three callers, so this one edit is executor/rollout lockstep.
    RefreshCityBlessing(state);
    RefreshStoriedLatch(state);   // storied: a land drop raises the permanent count too

    leave_zone();
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
        // GainLife bumps life_gained_this_turn (Kazandu Refuge feeds Fortifying Draught's X) and
        // fires any "whenever you gain life" watchers -- one gain event.
        GainLife(state, state.active_player_index, fdef.params.etb_lifegain);
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
    // LANDFALL (route 1 of OnLandEntered's contract, core/SpellEffects.h): every PLAYED land, in all
    // three callers' worlds at once -- this is THE land drop. After the land's own enter triggers
    // (scry, Karoo bounce, lifegain, Orchard spawn, snow scry), which is a CR 603.3b ordering among
    // simultaneous triggers that no landfall payload in the pool can observe. Keyed by the played
    // card's NUMBER and our controller, never by slot, so a Karoo that bounced ITSELF still triggers
    // (the trigger exists from the entry). Gated per game inside -> byte-identical without landfall.
    OnLandEntered(state, state.active_player_index, perm.card.m_number);
    return true;
}

bool PlayLandFromHand(GameState& state, std::size_t hand_index, const CardDefinition& def,
                      const LandPlayOptions& opts)
{
    Player& ap = state.ActivePlayer();
    if (hand_index >= ap.hand.size()) { return false; }
    auto it = ap.hand.begin() + static_cast<std::ptrdiff_t>(hand_index);
    return PlayLandCore(state, *it, def, opts, [&ap, it]() { ap.hand.erase(it); });
}

bool PlayLandFromLibraryTop(GameState& state, const CardDefinition& def, const LandPlayOptions& opts)
{
    Player& ap = state.ActivePlayer();
    if (ap.library.empty()) { return false; }
    // A raw pop, NOT a draw: no cards_drawn_this_turn, no EnterHand, no draw watcher -- the card goes
    // from the library straight to the battlefield, which is what playing it from the top is.
    const Card top = ap.library.DrawTop();
    LandPlayOptions o = opts;
    o.from_library_top = true;   // the log records the source (GameLogger::LogPlayLand)
    return PlayLandCore(state, top, def, o, []() {});
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
        // A life-cost cycle (Street Wraith) the payers would refuse (life <= its cost) is no outlet.
        // One they would take notes its mana part ({0}): reading TOO SMALL is this probe's
        // conservative direction (the rule declines), see `cheapest` above.
        if (d->params.cycling_cost.has_value()
            && CyclingLifePayable(state, state.active_player_index, *d))
        { note(EffectiveCyclingCostFor(state, *d).ManaValue()); }
        if (d->params.sacrifice_draw_cost.has_value())
        { note(d->params.sacrifice_draw_cost->ManaValue()); }
    }
    if (cheapest < 0) { return true; }        // nothing but lands in hand: nothing to spend on

    // Idle exactly when the untapped drop still cannot buy the cheapest thing available.
    return untapped_net < cheapest;
}

// The ranker body behind GreedyLandChoiceIndex and GreedyLandChoice. `top` is the library top when
// it is a playable land (Courser of Kruphix), else nullptr -- in which case this is EXACTLY the
// hand-only ranker it was factored out of. The top is judged by the same per-card tests as a hand
// card (it is a candidate "in the same four passes"); within a pass it is tried BEFORE the hand when
// `top_first` (the provider's tie rule), after it otherwise.
static GreedyLandPick GreedyLandRank(const GameState& state, const Card* top, bool top_first)
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
            return GreedyLandPick{ i, false };
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
    // MTG_LAND_BOUNCE_GUARD (default ON; =0 -> any other land suffices). "Another land exists" is not
    // enough: when the land the bounce would RETURN is itself a Karoo, the drop gains nothing -- the
    // board ends with one bounce land, entered tapped, and the other back in hand -- while a real land
    // may be waiting. Bruna d0 gi1933 (seed 1091934) replayed Azorius Chancery over its twin every turn
    // with Remote Farm in hand. The returned land is predicted by the very ranking BounceKarooLand
    // takes its pick from (BounceLandCandidates' front), which already avoids a Karoo whenever anything
    // else is there -- so this refuses exactly the Karoo-for-Karoo swap. Lazy: only asked when a bounce
    // land is the candidate under the scan.
    int karoo_swap = -1;   // -1 not asked yet, 0 no, 1 the bounce would return a Karoo
    auto karoo_returns_karoo = [&]() -> bool
    {
        if (karoo_swap >= 0) { return karoo_swap == 1; }
        karoo_swap = 0;
        static const bool s_on = EnvOn("MTG_LAND_BOUNCE_GUARD", true);
        if (!heurarm::Flag(heurarm::LAND_BOUNCE_GUARD, s_on)) { return false; }
        std::vector<int> legal;
        for (int i = 0; i < static_cast<int>(state.battlefield.size()); ++i)
        {
            const Permanent& p = state.battlefield[static_cast<std::size_t>(i)];
            if (p.controller_index == state.active_player_index && p.card.IsLand()) { legal.push_back(i); }
        }
        if (legal.empty()) { return false; }
        const std::vector<int> ranked = ResolveProvider(state).BounceLandCandidates(
            state, state.active_player_index, /*self_index=*/-1, legal);
        if (ranked.empty()) { return false; }
        const CardDefinition* rd = CardDatabase::Instance().LookupCached(
            state.battlefield[static_cast<std::size_t>(ranked.front())].card);
        if (rd != nullptr && rd->params.etb_bounce_land) { karoo_swap = 1; }
        return karoo_swap == 1;
    };

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
    if (TopResolveActive(state))
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
    // WHY IT MATTERS HERE. On Fluctuator 34 of 42 lands enter tapped, so two mana on turn two IS
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
        // The library top (Courser) is one more candidate in THIS pass: index -1 in the walk below.
        // Visited first or last within the pass per the provider's tie rule; absent (nullptr) for
        // every deck without a play-from-top permanent, which leaves the walk exactly as it was.
        const int first = (top != nullptr && top_first) ? -1 : 0;
        const int last  = (top != nullptr && !top_first) ? n : n - 1;
        for (int k = first; k <= last; ++k)
        {
            const bool is_top = (k < 0 || k >= n);
            const int  i      = is_top ? -1 : k;
            const Card& cand  = is_top ? *top : ap.hand[i];
            if (cand.m_impulse_no_land) { continue; }
            const CardDefinition* front = CardDatabase::Instance().LookupCached(cand);
            const CardDefinition* def   = LandFaceDefOf(front);   // spell//land plays its back face
            if (!def) { continue; }
            if (hold_top_consumer && front != nullptr
                && front->params.look_top_put_creature_count > 0) { continue; }   // combo piece, not the drop
            if (def->params.etb_bounce_land && (!has_other_land || karoo_returns_karoo())) { continue; }
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
            return GreedyLandPick{ i, is_top };
        }
        }
    }
    return GreedyLandPick{};
}

int GreedyLandChoiceIndex(const GameState& state)
{
    return GreedyLandRank(state, nullptr, false).hand_index;
}

GreedyLandPick GreedyLandChoice(const GameState& state)
{
    // No playable top (every deck without a play-from-top permanent: one bool test) -> the hand ranker.
    if (PlayableTopLandNumber(state) == 0) { return GreedyLandRank(state, nullptr, false); }
    const Card& top = state.ActivePlayer().library.front();
    return GreedyLandRank(state, &top,
                          ResolveProvider(state).TopLandSourceFirstOnTie(state, state.active_player_index));
}

// ---- THE ACTIVATION-COLOUR NEED (MTG_ROLLOUT_LAND_ACT_COLOR) -------------------------------------
//
// WHICH COSTS. Every battlefield activated ability whose cost the engine models as a ManaCost: the
// PermAbilityMode table CollectActivationKeys enumerates, plus the blink / pump / pod / sac-outlet /
// animate / equip-all / put-from-hand / graveyard-grow / reveal-top / untap / tap-token / aura-swap
// costs. There is no generic "activation cost" probe (LandPlay's own FirstDropManaWouldSitIdle note
// says so), so this is a list -- but a list in the SAFE direction: a cost it misses yields no need,
// so the rule does not fire and the drop is the legacy one. A miss can never manufacture a fire,
// which is the opposite of the idle-mana rule's exposure and why a list is acceptable here.
//
// HARD PIPS ONLY. Hybrid and Phyrexian pips are baked into the flat colour ints (Card.h), but
// neither is a hard requirement -- {G/W} pays with either colour, {G/P} with 2 life -- so both are
// subtracted back out. Twobrid and snow pips already live in `generic`.
int UnmetActivationColorMask(const GameState& state)
{
    const int active = state.active_player_index;
    int need = 0;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != active) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d == nullptr) { continue; }
        const CardParams& q = d->params;
        const std::optional<ManaCost>* costs[] = {
            // The PermAbilityMode sinks (CollectActivationKeys' table, same order).
            &q.tap_damage_cost, &q.tap_investigate_cost, &q.tap_draw_cost, &q.sac_draw_cost,
            &q.drain_cost, &q.exile_opponent_top_cost, &q.ice_counter_cost, &q.lifelink_grant_cost,
            &q.pay_token_cost, &q.ping_all_cost, &q.life_gated_put_creatures_cost,
            &q.sac_lifegain_per_creature_cost, &q.tap_lifegain_cost, &q.activated_dig_cost,
            // The other battlefield activations.
            &q.blink_cost, &q.team_pump_cost, &q.pod_activation_cost, &q.sac_creature_cost,
            &q.firebreathing_cost, &q.animate_cost, &q.tap_token_cost, &q.attach_all_equipment_cost,
            &q.tap_put_from_hand_cost, &q.gy_exile_grow_cost, &q.activated_reveal_top_cost,
            &q.untap_creature_cost,
            // Arcanum Wings' swap is an activation only while the Aura is attached.
            p.aura_attached_to != 0 ? &q.aura_swap_cost : nullptr,
        };
        for (const std::optional<ManaCost>* c : costs)
        {
            if (c == nullptr || !c->has_value()) { continue; }
            const ManaCost m = EffectiveActivationCost(state, active, p.card, c->value());
            int pip[6] = { m.white, m.blue, m.black, m.red, m.green, m.colorless };
            for (int h = 0; h < m.hybrid_count && h < 4; ++h)
            {
                const int first = (m.hybrid_pair[h] >> 4) & 0x0F;
                if (first >= 0 && first < 6) { --pip[first]; }
            }
            for (int h = 0; h < m.phyrexian_count && h < 2; ++h)
            {
                const int col = m.phyrexian_color[h];
                if (col >= 0 && col < 6) { --pip[col]; }
            }
            for (int i = 0; i < 6; ++i) { if (pip[i] > 0) { need |= (1 << i); } }
        }
    }
    if (need == 0) { return 0; }
    for (int i = 0; i < 6; ++i)
    {
        if ((need & (1 << i)) && BoardCanProduceColor(state, active, static_cast<Color>(i)))
        { need &= ~(1 << i); }
    }
    return need;
}

int ActivationColorLandIndex(const GameState& state, int preferred)
{
    const int need = UnmetActivationColorMask(state);
    if (need == 0) { return -1; }
    const Player& ap = state.ActivePlayer();
    const int active = state.active_player_index;
    const int n = static_cast<int>(ap.hand.size());

    // The colours a hand card makes as a land (its land face; a hand Aether Hub keeps its coloured
    // modes because it brings its own energy -- EffectiveProduces' in_hand rule).
    const auto supplies = [&](int i) -> bool {
        if (i < 0 || i >= n) { return false; }
        const CardDefinition* def = LandFaceDefOf(CardDatabase::Instance().LookupCached(ap.hand[i]));
        if (def == nullptr) { return false; }
        for (Color c : EffectiveProduces(state, active, *def, /*in_hand=*/true))
        { if (need & (1 << static_cast<int>(c))) { return true; } }
        return false;
    };
    if (preferred >= 0 && preferred < n && !ap.hand[static_cast<std::size_t>(preferred)].m_impulse_no_land
        && supplies(preferred))
    { return preferred; }

    bool has_other_land = false;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index == active && p.card.IsLand()) { has_other_land = true; break; }
    }
    // The rollout's legacy two-pass order (SimulateLandPlay), restricted to the supplying lands.
    for (int pass = 0; pass < 2; ++pass)
    {
        for (int i = 0; i < n; ++i)
        {
            if (ap.hand[i].m_impulse_no_land) { continue; }
            const CardDefinition* def = LandFaceDefOf(CardDatabase::Instance().LookupCached(ap.hand[i]));
            if (def == nullptr) { continue; }
            if (def->params.etb_bounce_land && !has_other_land) { continue; }
            if (pass == 0 && def->params.produces.size() <= 1) { continue; }
            if (supplies(i)) { return i; }
        }
    }
    return -1;
}
