// Unified land drop -- see LandPlay.h for why this exists and what each caller passes.
#include "../core/EnvFlags.h"
#include "LandPlay.h"
#include "../cards/CardDatabase.h"
#include "../core/GameLogger.h"
#include "../core/RolloutTouch.h"
#include "../core/SpellEffects.h"

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
    if (opts.land_face == "back" && !def.params.mdfc_back_name.empty())
    {
        const CardDefinition* bd = CardDatabase::Instance().Lookup(def.params.mdfc_back_name);
        if (bd) { face_def = bd; }
    }

    // Resolve "as this land enters" choices (shock life payment, reveal-untap) while the card is
    // still in hand; this also tells us whether it enters tapped.
    bool tapped;
    if (opts.honor_entry_chooser && g_play_land_entry_chooser && LandEntryHasChoice(state, def))
    {
        bool heur_untapped = !LandWouldEnterTapped(state, def, opts.allow_shock_pay);
        bool untapped = (*g_play_land_entry_chooser)(
            state, state.active_player_index, def.card.m_name.str(),
            def.params.etb_pay_life_to_untap,
            def.params.etb_untap_reveal_subtypes, heur_untapped);
        if (untapped) { ApplyLandUntapPayment(state, def); }
        tapped = !untapped;
    }
    else
    {
        tapped = LandEntersTapped(state, def, opts.allow_shock_pay);
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
    if (def.params.enters_tapped_with_depletion > 0)
    {
        Counter dep;
        dep.type  = Counter::Type::Depletion;
        dep.count = def.params.enters_tapped_with_depletion;
        perm.counters.push_back(dep);
    }
    state.battlefield.push_back(perm);

    ap.hand.erase(it);
    ++ap.lands_played_this_turn;

    // ETB effects, after the land is on the battlefield.
    if (def.params.etb_scry > 0)
    {
        if (opts.label_look_source) { ScryTop(state, def.params.etb_scry, def.card.m_name); }
        else                        { ScryTop(state, def.params.etb_scry); }
    }
    if (def.params.etb_surveil > 0)
    {
        if (opts.label_look_source) { SurveilTop(state, def.params.etb_surveil, def.card.m_name); }
        else                        { SurveilTop(state, def.params.etb_surveil); }
    }
    if (def.params.etb_bounce_land)
    {
        BounceKarooLand(state, state.active_player_index,
                        static_cast<int>(state.battlefield.size()) - 1);
    }
    // Forbidden Orchard played this turn: it is tapped for mana this turn too, so spawn the
    // opponent's Spirit now (the turn-start spawn only covers copies already in play).
    if (opts.spawn_orchard_spirit && IsForbiddenOrchard(&def)) { SpawnOpponentSpirit(state); }
    return true;
}
