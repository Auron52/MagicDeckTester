#pragma once
// THE passive opponent's library and opening hand.
//
// Before this existed, `GoldFishRunner::SetupGame` gave the opponent exactly one thing -- a life
// total. `players[1].library` was empty from turn 0 and never filled, the opponent never became the
// active player so no draw step ever ran for them, and nothing anywhere in src/ wrote to
// `players[1]` except `.life`. That silently deleted a whole class of win condition: milling or
// decking the opponent could not win and could not even be ATTEMPTED, because there was no draw for
// them to fail. See docs/design/passive-opponent-no-library.md for how that presented -- a deck
// whose intended kill was "deck the opponent" measured 2.5 turns slower than the user expected,
// with nothing in any report pointing at why.
//
// Decks that observe opponent land development choose a real 60-card mainboard
// by seed % 50 from the frozen primary decklist pool. Other decks with opponent
// zone effects retain the established fixed list. The opponent stays passive.
// Every library card is a name-only placeholder; read its types from CardDatabase at use time.
#include "GameState.h"
#include "DrawCap.h"
#include "OpponentDeckPool.h"
#include "../cards/CardDatabase.h"
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace opponentdeck {

inline const DeckSpec& Selected(uint64_t seed) { return Pool()[seed % Pool().size()]; }
inline const std::vector<std::pair<const char*, int>>& FixedList()
{
    static const std::vector<std::pair<const char*, int>> kList = {
        {"Mountain", 8}, {"Forest", 8}, {"Island", 4}, {"Plains", 4},
        {"Birds of Paradise", 4}, {"Monastery Swiftspear", 4},
        {"Sinew Sliver", 4}, {"Venerable Knight", 4}, {"Goblin Guide", 4},
        {"Lightning Bolt", 4}, {"Swords to Plowshares", 4},
        {"Ponder", 4}, {"Sol Ring", 4},
    };
    return kList;
}
inline const std::vector<std::pair<const char*, int>>& List(uint64_t seed,
                                                             bool use_diverse_pool = false)
{ return use_diverse_pool ? Selected(seed).cards : FixedList(); }

// Cards drawn into the opponent's opening hand. Seven, like anybody else's; its
// deterministic land policy does not include mulligans.
inline constexpr int kOpeningHandSize = 7;

// Numbers assigned to the opponent's cards, offset far above any deck's own numbering so an
// opponent card can never be confused with ours in a log, a reveal, or a viewer click. Player 0's
// numbering is per-deck and dense from 1; nothing in the engine numbers past a few hundred.
inline constexpr int kNumberBase = 100000;

// Deal the library and opening hand. No-op for decks that neither touch those
// zones nor observe the opponent's land development.
//
// The shuffle takes a DERIVED seed through a dedicated salt, and that detail is what makes this a
// non-event for existing decks: Library::Shuffle takes an EXPLICIT seed rather than drawing from a
// shared RNG stream, so dealing a second library cannot perturb player 0's permutation by a single
// card. (Had it been a shared stream this change would have moved every deck's baseline and cost a
// full three-tier GT rebaseline.)
inline void Deal(GameState& state, bool needed, uint64_t seed, bool use_diverse_pool = false)
{
    if (!needed) { return; }

    Player& opp = state.players[1];
    int number  = kNumberBase;
    for (const std::pair<const char*, int>& entry : List(seed, use_diverse_pool))
    {
        for (int i = 0; i < entry.second; ++i)
        {
            Card c;
            c.m_name = entry.first;
            c.RehashName();
            c.m_number = number++;
            opp.library.push_back(c);
        }
    }

    // 0xDEC7 ("deck"): any nonzero constant works -- what matters is that it is NOT the opening
    // salt, so the two shuffles are independent realisations of the same game seed.
    opp.library.Shuffle(SaltSeed(seed, 0xDEC7ull));

    // A flat 7. Their deterministic policy does not mulligan; the hand exists
    // for land plays and discard effects.
    for (int i = 0; i < kOpeningHandSize && !opp.library.empty(); ++i)
    {
        opp.hand.push_back(opp.library.front());
        opp.library.erase(opp.library.begin());
    }

    // SET LAST, AND ONLY HERE. The flag means "a library was actually dealt", so the ONLY code that
    // may raise it is the code that fills the zone. This is not tidiness -- it closes the
    // empty-is-not-absent trap structurally. It was previously set by StampDeckTraits, and the
    // scenario harness calls StampDeckTraits WITHOUT calling SetupGame: every EDF fixture came up
    // with the flag true and the library empty, so the very first end-of-turn draw decked the
    // opponent and two fixtures that assert "no win" reported wins on turns 3 and 5. Any future
    // caller that builds a GameState by hand now simply gets the old, safe model.
    state.opponent_library_dealt = true;
    state.opponent_plays_lands = use_diverse_pool;
}

struct LandPlayResult
{
    bool played = false;
    bool searched = false;
    std::string land_name;
    std::string fetched_name;
};

// The passive land policy: play the leftmost land in hand, then crack
// an immediately usable fetch. Choose the first eligible library land; this is
// an opponent policy, not a search branch for our solver. The caller performs
// the shuffle and offers the Archive Trap instant window after resolution.
inline LandPlayResult PlayLeftmostLand(GameState& state)
{
    LandPlayResult result;
    if (!state.opponent_plays_lands || state.opponent_decked) { return result; }
    Player& opp = state.players[1];
    auto held = std::find_if(opp.hand.begin(), opp.hand.end(), [](const Card& c) {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        return d != nullptr && d->card.IsLand();
    });
    if (held == opp.hand.end()) { return result; }
    const Card card = *held;
    const CardDefinition* def = CardDatabase::Instance().LookupCached(card);
    opp.hand.erase(held);
    result.played = true;
    result.land_name = card.m_name.str();
    auto enter = [&](const Card& entering, const CardDefinition& land_def, bool force_tapped) {
        Permanent perm;
        perm.card = land_def.card;
        perm.card.m_number = entering.m_number;
        perm.controller_index = 1;
        perm.owner_index = 1;
        perm.entered_this_turn = true;
        // The passive opponent declines optional shock-land life payments.
        perm.tapped = force_tapped || land_def.params.enters_tapped
            || land_def.params.etb_pay_life_to_untap > 0;
        state.battlefield.push_back(std::move(perm));
    };
    const bool typed_fetch = !def->params.fetch_land_types.empty();
    const bool basic_fetch = def->params.land_search_activation_sacrifice_self
        && def->params.land_search_basic_only && def->params.land_search_count == 1
        && def->params.land_search_activation_cost.has_value()
        && def->params.land_search_activation_cost->ManaValue() == 0;
    // Older typed-fetch definitions omit the activation fields because the
    // normal fetch resolver charges their printed one life directly.
    const int life_cost = typed_fetch
        ? std::max(1, def->params.land_search_activation_life_cost)
        : def->params.land_search_activation_life_cost;
    if ((!typed_fetch && !basic_fetch) || opp.life <= life_cost)
    {
        enter(card, *def, false);
        return result;
    }
    // Fetch activation: tap and sacrifice this just-played land as costs.
    opp.life -= life_cost;
    opp.graveyard.push_back(card);
    result.searched = true;
    auto pick = std::find_if(opp.library.begin(), opp.library.end(), [&](const Card& candidate) {
        const CardDefinition* land = CardDatabase::Instance().LookupCached(candidate);
        if (land == nullptr || !land->card.IsLand()) { return false; }
        if (basic_fetch) { return land->card.HasSupertype(Supertype::Basic); }
        for (const std::string& type : def->params.fetch_land_types)
        for (const std::string& subtype : land->card.m_subtypes)
        { if (type == subtype) { return true; } }
        return false;
    });
    if (pick != opp.library.end())
    {
        const Card fetched = *pick;
        const CardDefinition* land = CardDatabase::Instance().LookupCached(fetched);
        opp.library.erase(pick);
        result.fetched_name = fetched.m_name.str();
        // A passive opponent never pays a shock land's optional two life.
        enter(fetched, *land, basic_fetch && def->params.land_search_tapped);
    }
    return result;
}

// The opponent's DRAW, simulated at the end of each of OUR turns.
//
// There is no opponent turn to hang it on: the goldfish takes every turn and the opponent never
// becomes the active player, so GameEngine::DrawStep never runs for them. End-of-our-turn is the
// faithful slot either way -- on the play, their turn N follows our turn N; on the draw, their turn
// N+1 follows our turn N, and their turn 1 (where THEY are on the play) correctly draws nothing.
//
// It is also what makes the win turn come out right, with no special-casing anywhere. The user's
// rule (2026-09-02): "the win turn for the case where the opponent decks out should be listed as
// your last turn ... because the user doesn't get any main phases." Firing here sets
// opponent_decked while state.turn_number is still OUR turn, so the caller's ordinary win check
// reports that turn.
//
// Called from BOTH worlds -- the executor's end step and the rollout's turn boundary. A draw the
// rollout does not simulate is a deck-out the search cannot SEE, which leaves an executor that
// recognises a win it never steers toward: the Dragonstorm go-off failure exactly.
inline void EndOfTurnDraw(GameState& state)
{
    if (!state.opponent_library_dealt || state.opponent_decked) { return; }
    // The per-turn draw cap (core/DrawCap.h): this is the first draw of THEIR turn (drawcap::TurnBoundary
    // opened it), so it never binds today; tested first because an ignored draw is no deck-out.
    if (drawcap::Blocks(state, 1)) { return; }
    Player& opp = state.players[1];
    if (opp.library.empty())
    {
        // CR 104.3c: a player who would draw from an empty library loses the game the next time a
        // player would receive priority. Nothing here can respond, so it is immediate.
        //
        // NOTE the asymmetry with TakeFromTop below: running the library OUT is not the loss, being
        // ASKED to draw from it is. So the last card is drawn normally and the deck-out lands on the
        // NEXT draw -- one turn later. Do not "simplify" these two into one empty-check.
        state.opponent_decked = true;
        return;
    }
    opp.hand.push_back(opp.library.front());
    opp.library.erase(opp.library.begin());
    // Counted for the cap only (a Howling Mine draw that follows on their turn is their second). Deck-gated:
    // every other deck leaves the opponent's count untouched, exactly as before.
    if (state.deck_has_draw_cap) { ++opp.cards_drawn_this_turn; }
}

// The passive opponent's cleanup, after all of its notional draws and end-step effects.
// It makes no discard decisions: repeatedly move the first card in hand to its graveyard.
// Keeping this separate from EndOfTurnDraw also covers extra draws such as Howling Mine.
inline void EndOfTurnCleanup(GameState& state)
{
    if (!state.opponent_library_dealt || state.opponent_decked) { return; }
    Player& opp = state.players[1];
    while (opp.hand.size() > 7)
    {
        opp.graveyard.push_back(opp.hand.front());
        opp.hand.erase(opp.hand.begin());
    }
}

// THE mill / exile-from-library primitive: take `n` cards off the top of the opponent's library
// into the shared exile zone (CR 406.1). Returns the number actually taken.
//
// Every effect that removes cards from the opponent's library must go through here, because of the
// second half: emptying the library **immediately** sets `opponent_decked`.
//
// That is not the CR rule (running out is not a loss; being asked to draw from empty is) but it is
// EXACT under this model, and the difference matters enormously to the search. Our turn always ends
// with their draw, and nothing can refill the zone, so a library emptied at any point during turn T
// loses them the game at the end of turn T -- the same win turn either way. Setting the flag at
// empty-time is what lets the SEARCH see it: `wins_this_turn`, the go-off short-circuit and every
// mid-turn OpponentHasLost check run before that end-of-turn draw, so deferring the flag would make
// a mill kill invisible to every within-turn win check and the search would never steer toward it.
// That is precisely the Dragonstorm go-off that executed as a kill zero times.
inline int TakeFromTop(GameState& state, int n)
{
    if (!state.opponent_library_dealt || n <= 0) { return 0; }
    Player& opp = state.players[1];
    int taken = 0;
    for (; taken < n && !opp.library.empty(); ++taken)
    {
        state.exile.push_back(opp.library.front());
        opp.library.erase(opp.library.begin());
    }
    if (opp.library.empty()) { state.opponent_decked = true; }
    return taken;
}

// Literal mill: cards move from the opponent's library into their graveyard.
// Keep the same immediate deck-out projection as TakeFromTop: the passive
// opponent's next draw is unavoidable before our next turn.
inline int MillToGraveyard(GameState& state, int n)
{
    if (!state.opponent_library_dealt || n <= 0) { return 0; }
    Player& opp = state.players[1];
    int milled = 0;
    for (; milled < n && !opp.library.empty(); ++milled)
    {
        opp.graveyard.push_back(opp.library.front());
        opp.library.erase(opp.library.begin());
    }
    if (opp.library.empty()) { state.opponent_decked = true; }
    return milled;
}

// THE forced-draw primitive: the opponent draws `n` cards ("Target player draws X cards" aimed at
// them -- Stroke of Genius). Returns the number actually drawn. Called from BOTH worlds through the
// one shared resolver (PerformDrawX, SpellEffects.h), never open-coded.
//
// Two ways this decks them, and both set `opponent_decked` HERE, at resolution:
//   * n > library: they are asked to draw from an empty library, which is the rules loss itself
//     (CR 121.4 / 104.3c, checked as a state-based action, CR 704.5b, the next time a player would
//     receive priority -- i.e. immediately after this spell resolves).
//   * n == library: the library is EMPTY but nobody has drawn from it yet. That is not the CR loss,
//     but under this model it is the SAME win on the SAME turn, for exactly TakeFromTop's reason:
//     our turn always ends with EndOfTurnDraw, nothing can refill the zone, so the loss lands at the
//     end of THIS turn -- and the reported win turn is this turn either way (EndOfTurnDraw fires
//     while turn_number is still ours). Setting the flag now is what lets every within-turn win
//     check (wins_this_turn, the go-off short-circuit, the rollout's OpponentHasLost) SEE the kill.
//     So the minimal lethal X is max(1, library) -- one card less than "library + 1".
// n <= 0 (an X = 0 cast) draws nothing and decks nobody, even against an empty library: no draw was
// asked for. Inert when no library was dealt (the empty-is-not-absent trap, see Deal).
inline int ForcedDraw(GameState& state, int n)
{
    if (!state.opponent_library_dealt || n <= 0) { return 0; }
    // The per-turn draw cap (core/DrawCap.h): draws past it are IGNORED, so they neither fill the hand nor
    // ask for a card from an empty library. n is the identity for every deck without a cap card.
    n = drawcap::Allowed(state, 1, n);
    if (n <= 0) { return 0; }
    Player& opp = state.players[1];
    int drawn = 0;
    for (; drawn < n && !opp.library.empty(); ++drawn)
    {
        opp.hand.push_back(opp.library.front());
        opp.library.erase(opp.library.begin());
    }
    if (state.deck_has_draw_cap) { opp.cards_drawn_this_turn += drawn; }
    if (opp.library.empty()) { state.opponent_decked = true; }
    return drawn;
}

// The smallest X that decks the opponent through ForcedDraw this turn, or 0 when no X can (no
// library dealt, or they are already decked). Shared by the enumerator's lethal-X sizing and the
// provider's prune, so the two cannot disagree about what "lethal" means.
inline int ForcedDrawLethalX(const GameState& state)
{
    if (!state.opponent_library_dealt || state.opponent_decked) { return 0; }
    const int lib = static_cast<int>(state.players[1].library.size());
    // Under a per-turn draw cap only the draws they may still make this turn count: no X decks them
    // when the library holds more than that (ForcedDraw ignores the rest).
    if (drawcap::Allowed(state, 1, std::max(1, lib)) < std::max(1, lib)) { return 0; }
    return std::max(1, lib);
}

} // namespace opponentdeck
