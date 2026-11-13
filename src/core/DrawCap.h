#pragma once
#include "GameState.h"
#include "../cards/CardDatabase.h"

#include <algorithm>

// THE PER-TURN DRAW CAP (Spirit of the Labyrinth: "Each player can't draw more than one card each turn";
// CardParams::each_player_draw_cap_per_turn). Spirits 2026-11-13, integrator F.
//
// THE RULE. While a cap permanent is on the battlefield (anyone's -- "each player"), a player who has
// already drawn `cap` cards this turn cannot draw: the instruction to draw is IGNORED for each card past
// the cap (CR 101.2 "can't" wins; it is not a replacement effect). Consequences the engine models:
//   * the count includes draws made BEFORE the cap permanent entered this turn (Player::
//     cards_drawn_this_turn, which every real draw site already maintains);
//   * an ignored draw is not an attempt to draw from an empty library, so it never decks anyone;
//   * a replacement effect cannot replace a draw that cannot happen (dredge, Unpredictable Cyclone),
//     so the cap is tested BEFORE any draw replacement is offered;
//   * effects that put cards into hand WITHOUT drawing (tutors, bounces, reveal-and-put, Recur) are
//     untouched -- the cap is consulted only for HandEntryReason::Draw / DrawStep;
//   * a loot's discard ("draw, then discard") still happens: only the draw is ignored.
//
// THE CHOKE POINT is AttemptDrawIntoHand (HandEntry.h), which every draw in BOTH worlds goes through
// (DrawIntoHand / DrawIntoHandDetailed / TrickDraw / PerformDrawX / the dig loops / the draw step). The
// sites that test the library for deck-out BEFORE calling it (Howling Mine, Horn of Greed) and the
// opponent's raw draws (opponentdeck::EndOfTurnDraw / ForcedDraw) consult DrawCapBlocks directly.
//
// THE TURN. "Each turn" is per turn, and the passive opponent's notional turn falls between ours. The
// executor and the rollout both run it in one shared-shape block (GameEngine::RunTurnFrom's and
// TurnSolver::SimulateEndAndStartNextTurn's `opponent_turn`), so DrawCapTurnBoundary is called at the
// start of that block AND at our untap: both players' counts restart at each of the two turn starts.
// The opponent's notional draw (EndOfTurnDraw) is counted, so a Howling Mine draw on their turn is capped.
//
// GATED on the deck constant GameState::deck_has_draw_cap (StampDeckTraits: a cap card in the mainboard
// or sideboard). Every other deck returns at the first test, so its play is byte-identical; nothing new
// is folded for them. For a cap deck, BuildSimKeyBase folds cards_drawn_this_turn (Dominance.h already
// folds it unconditionally): the count decides whether a later draw this turn happens.
namespace drawcap
{
    // The tightest cap any battlefield permanent imposes (0 = none).
    inline int CapPerTurn(const GameState& state)
    {
        if (!state.deck_has_draw_cap) { return 0; }
        int cap = 0;
        for (const Permanent& perm : state.battlefield)
        {
            if (perm.def_absent) { continue; }
            const CardDefinition* d = CardDatabase::Instance().LookupCached(perm.card);
            if (d == nullptr) { continue; }
            const int c = d->params.each_player_draw_cap_per_turn;
            if (c > 0) { cap = cap == 0 ? c : std::min(cap, c); }
        }
        return cap;
    }

    // True when `player`'s next draw is ignored. `prior_batch_draws` = draws already made by the
    // enclosing multi-draw effect that its caller has not yet added to cards_drawn_this_turn.
    inline bool Blocks(const GameState& state, int player, int prior_batch_draws = 0)
    {
        const int cap = CapPerTurn(state);
        return cap > 0
            && state.players[static_cast<std::size_t>(player)].cards_drawn_this_turn + prior_batch_draws >= cap;
    }

    // How many of `n` draws `player` may still make this turn (n when uncapped). For PREDICTORS that
    // price a draw before it happens (the resolution itself goes through the choke point).
    inline int Allowed(const GameState& state, int player, int n)
    {
        if (n <= 0) { return n; }
        const int cap = CapPerTurn(state);
        if (cap <= 0) { return n; }
        const int left = cap - state.players[static_cast<std::size_t>(player)].cards_drawn_this_turn;
        return std::max(0, std::min(n, left));
    }

    // A turn starts (ours at untap, or the opponent's notional one): every player's count restarts. The
    // Niv draw-trigger watermark follows the active player's count down (its invariant is mark <= count;
    // every draw of the ending turn was flushed at its own resolution boundary).
    inline void TurnBoundary(GameState& state)
    {
        if (!state.deck_has_draw_cap) { return; }
        for (Player& p : state.players) { p.cards_drawn_this_turn = 0; }
        state.draw_trigger_mark = 0;
    }
}
