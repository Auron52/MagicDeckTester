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
#include "EnvFlags.h"
#include "GameState.h"
#include "DrawCap.h"
#include "OpponentDeckPool.h"
#include "../cards/CardDatabase.h"
#include "../ai/EngineFlags.h"   // FreeTurnHurdlesOn: the free-turn rule's cleanup floor
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace opponentdeck {

inline const DeckSpec& Selected(uint64_t seed) { return Pool()[seed % Pool().size()]; }
// The pool slot of the list named `name` (DeckSpec::name, e.g. "White Knights"), or -1.
inline int SlotOfList(const std::string& name)
{
    for (std::size_t i = 0; i < Pool().size(); ++i)
    { if (name == Pool()[i].name) { return static_cast<int>(i); } }
    return -1;
}
// The list actually dealt to players[1] (2HG may pick the second slot; see Deal).
inline const DeckSpec& Selected(const GameState& state) { return Pool()[state.opponent_slot % Pool().size()]; }

inline constexpr std::uint8_t kNoOpponentSlot = 0xFF;

// A pool list's total printed mana value (lands count 0). 2HG's targeted head is the list with the
// LOWER total (USER 2026-11-17: "mill should just target one opponent, probably the one that has
// cheaper cards in deck (lower total MV)"). Unknown names count 0. Computed once per slot.
inline int ListTotalManaValue(std::size_t slot)
{
    static const std::vector<int> kTotals = [] {
        std::vector<int> totals;
        for (const DeckSpec& spec : Pool())
        {
            int total = 0;
            for (const std::pair<const char*, int>& e : spec.cards)
            {
                const CardDefinition* d = CardDatabase::Instance().Lookup(e.first);
                if (d != nullptr && !d->card.IsLand()) { total += e.second * d->card.m_mana_cost.ManaValue(); }
            }
            totals.push_back(total);
        }
        return totals;
    }();
    return slot < kTotals.size() ? kTotals[slot] : 0;
}

// 2HG's second list: a distinct slot drawn off a salted seed (the first is seed % 50, as for a
// single opponent).
inline std::size_t SecondSlot(uint64_t seed)
{
    const std::size_t n = Pool().size();
    const std::size_t a = seed % n;
    return (a + 1 + SaltSeed(seed, 0x2EADull) % (n - 1)) % n;
}
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
// 2HG's second head: its own band, still >= kNumberBase so every "is an opponent card" test holds.
inline constexpr int kNumberBase2 = 110000;

// Deal the library and opening hand. No-op for decks that neither touch those
// zones nor observe the opponent's land development.
//
// The shuffle takes a DERIVED seed through a dedicated salt, and that detail is what makes this a
// non-event for existing decks: Library::Shuffle takes an EXPLICIT seed rather than drawing from a
// shared RNG stream, so dealing a second library cannot perturb player 0's permutation by a single
// card. (Had it been a shared stream this change would have moved every deck's baseline and cost a
// full three-tier GT rebaseline.)
//
// heads >= 2 (2HG, pool decks only): a second list goes to the land-only second head
// (GameState::opponent2_*), and players[1] -- the head every library/hand effect addresses -- holds
// whichever of the two has the lower total mana value (ListTotalManaValue). heads == 1 is exactly
// the single-opponent deal.
//
// slot_override / slot2_override (>= 0, pool decks only) pin the dealt list(s) instead of deriving them
// from the seed; the seed still drives the shuffles, and the 2HG lower-total-MV rule still decides which
// pinned list is the targeted head. Only the scenario harness pins (a fixture's `opponent_list`): a real
// game always takes its lists from the seed.
inline void Deal(GameState& state, bool needed, uint64_t seed, bool use_diverse_pool = false,
                 int heads = 1, int slot_override = -1, int slot2_override = -1)
{
    if (!needed) { return; }

    Player& opp = state.players[1];
    int number  = kNumberBase;
    const std::size_t n_pool = Pool().size();
    std::size_t slot = (use_diverse_pool && slot_override >= 0)
        ? static_cast<std::size_t>(slot_override) % n_pool : seed % n_pool;
    const bool two_heads = use_diverse_pool && heads >= 2;
    std::size_t slot2 = !two_heads ? slot
        : slot2_override >= 0 ? static_cast<std::size_t>(slot2_override) % n_pool
        : slot_override >= 0 ? (slot + 1 + SaltSeed(seed, 0x2EADull) % (n_pool - 1)) % n_pool
        : SecondSlot(seed);
    if (two_heads && ListTotalManaValue(slot2) < ListTotalManaValue(slot)) { std::swap(slot, slot2); }
    state.opponent_slot = static_cast<std::uint8_t>(slot);
    const std::vector<std::pair<const char*, int>>& list
        = use_diverse_pool ? Pool()[slot].cards : FixedList();
    for (const std::pair<const char*, int>& entry : list)
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

    if (two_heads)
    {
        // The second head's own library, numbered in its own band, on its own salted shuffle.
        int number2 = kNumberBase2;
        for (const std::pair<const char*, int>& entry : Pool()[slot2].cards)
        {
            for (int i = 0; i < entry.second; ++i)
            {
                Card c;
                c.m_name = entry.first;
                c.RehashName();
                c.m_number = number2++;
                state.opponent2_library.push_back(c);
            }
        }
        state.opponent2_library.Shuffle(SaltSeed(seed, 0x2EAD2ull));
        for (int i = 0; i < kOpeningHandSize && !state.opponent2_library.empty(); ++i)
        {
            state.opponent2_hand.push_back(state.opponent2_library.front());
            state.opponent2_library.erase(state.opponent2_library.begin());
        }
        state.opponent2_slot = static_cast<std::uint8_t>(slot2);
    }
}

inline bool HasSecondHead(const GameState& state) { return state.opponent2_slot != kNoOpponentSlot; }

struct LandPlayResult
{
    bool played = false;
    bool searched = false;
    std::string land_name;
    std::string fetched_name;
    // What the land's entry did, in order, for the play-event sink ("enters as Turntimber, Serpentine
    // Wood (tapped)", "returns Forest to hand", "sacrifices Swamp", "gains 2 life"). Empty for a plain land.
    std::vector<std::string> notes;
    std::vector<std::string> fetched_notes;   // the same, for the land a fetch put onto the battlefield
    // ETB life gains (source name, amount) the caller applies through OpponentGainsLife after its events.
    std::vector<std::pair<std::string, int>> life_gains;
};

// 2HG HEAD OWNERSHIP. Both heads' permanents sit on the shared battlefield under controller 1; which
// head a card belongs to is its card-number band (Deal numbers head 1 from kNumberBase, head 2 from
// kNumberBase2). Anything else under controller 1 (a card of ours they gained, an untracked token, a
// hand-built test card) counts as head 1's: the targeted head that every single-opponent effect addresses.
inline constexpr int kHeadBandWidth = 10000;
inline int HeadOfNumber(int number)
{ return (number >= kNumberBase2 && number < kNumberBase2 + kHeadBandWidth) ? 2 : 1; }
inline bool HeadControls(const Permanent& p, int head)
{ return p.controller_index == 1 && HeadOfNumber(p.card.m_number) == head; }
// A nontoken land that head controls.
inline bool HeadControlsLand(const Permanent& p, int head)
{ return !p.is_token && p.card.IsLand() && HeadControls(p, head); }

// ORDER-CANONICAL PLACEMENT (MTG_OPP_LANDS_FRONT, DEFAULT ON; =0 restores push_back). The
// opponent's lands enter as ONE block at the FRONT of the shared battlefield, in their own arrival
// order, ahead of all of ours: the layout the legacy three-land board always had. push_back
// interleaved each opponent land between our permanents by the TURN we happened to cast them, so two
// lines reaching the same position ("cast X and Y on T2" vs "X on T2, Y on T3") differed in
// battlefield ORDER, and every order-exact consumer (FsOrderSig: the SimulateToEnd TT, FSL win lines,
// bp-enum plans, enumeration dedup) stopped sharing them. jumbo_regression_d5_s30001 gi11 (budget 0):
// the T1 decision's rollout work rose 3.2x over the legacy board. This is an identity fold, not a
// narrowing: each side's permanents keep their relative order, the board's contents are unchanged,
// and nothing holds a battlefield index across the opponent's turn (attachments, targets and linked
// exiles name card numbers), so only the interleave -- which no rule reads -- moves.
inline bool OpponentLandsFrontOn()
{
    static const bool v = EnvOn("MTG_OPP_LANDS_FRONT", true);   // DEFAULT ON; =0 disables
    return v;
}

// PlayLeftmostLandFrom / PlayLeftmostLand -- the land policy itself -- live in SpellEffects.h, beside
// ResolvePassiveOpponentLandTurn: a land's entry (an MDFC's land face, a Karoo bounce, Lake of the Dead's
// replacement, an enters-tapped condition, an ETB life gain) reads the shared land helpers defined there.

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
// 2HG's second head draws for its notional turn too. Running ITS library out is a loss for the
// team as well (CR 810.8a: a team loses when either player loses), on the same next-draw rule as
// head 1. Draw caps and Howling Mine address players[1] only (a stated limit of the land-only head).
inline void SecondHeadDraw(GameState& state)
{
    if (!HasSecondHead(state) || state.opponent_decked) { return; }
    if (state.opponent2_library.empty()) { state.opponent_decked = true; return; }
    state.opponent2_hand.push_back(state.opponent2_library.front());
    state.opponent2_library.erase(state.opponent2_library.begin());
}

inline void EndOfTurnDrawHead1(GameState& state);
inline void EndOfTurnDraw(GameState& state)
{
    EndOfTurnDrawHead1(state);
    SecondHeadDraw(state);
}

inline void EndOfTurnDrawHead1(GameState& state)
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
//
// THE HAND'S TWO SIDES under the free-turn rule (USER 2026-11-22: "I guess a MDFC that is both should count as
// whatever you have less of."). Shared by the lock (core/FreeTurns.h HeadLocked), the opening-hand hurdles and the
// cleanup floor below, so the three can never disagree. Every card that is not a spell//land MDFC counts by its
// front face; then each MDFC joins whichever side is SMALLER at that point, a TIE going to LAND (agent default,
// reported). So an MDFC fills an empty side: {Bolt, MDFC} is 1 land + 1 nonland; {Mountain, MDFC} likewise;
// a hand holding only {MDFC} is 1 land + 0 nonlands (the tie).
struct HandSides { int lands = 0; int nonlands = 0; };
// A spell//land modal double-faced card: a nonland front whose back face is a land (Turntimber Symbiosis).
inline bool IsSpellLandMdfc(const CardDefinition* def)
{
    if (def == nullptr || def->card.IsLand() || def->params.mdfc_back_name.empty()) { return false; }
    const CardDefinition* back = CardDatabase::Instance().Lookup(def->params.mdfc_back_name);
    return back != nullptr && back->card.IsLand();
}
inline bool HandCardIsLandFront(const Card& c)
{
    const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
    return d != nullptr ? d->card.IsLand() : c.IsLand();
}
// skip: an index to leave out (the cleanup's "what if this card were shed"), or -1.
inline HandSides CountHandSides(const std::vector<Card>& hand, int skip = -1)
{
    HandSides h;
    int mdfcs = 0;
    for (int i = 0; i < static_cast<int>(hand.size()); ++i)
    {
        if (i == skip) { continue; }
        const Card& c = hand[static_cast<std::size_t>(i)];
        if (IsSpellLandMdfc(CardDatabase::Instance().LookupCached(c))) { ++mdfcs; }
        else if (HandCardIsLandFront(c)) { ++h.lands; }
        else { ++h.nonlands; }
    }
    for (; mdfcs > 0; --mdfcs) { if (h.lands <= h.nonlands) { ++h.lands; } else { ++h.nonlands; } }
    return h;
}

// UNDER THE FREE-TURN RULE (FreeTurnHurdlesOn AND the deck's lock trait, GameState::deck_has_free_turn) the cleanup
// keeps the flood hurdle's floor: it still sheds the oldest card, except that a shed may not take the hand below
// kCleanupNonlandFloor (2) nonlands (CountHandSides) -- then it sheds the oldest card whose loss keeps the nonland
// count, i.e. the oldest land (CleanupShedIndex). Plain oldest-first shed the opening hand's nonlands -- the two the
// hurdle guarantees -- once one of OUR cards grew the opponent's hand past seven (Goblin Guide's reveal, Howling
// Mine, Horn of Greed), so a land-heavy list kept a hand of lands only and "locked" with no lock card in play
// (Burn s3664 gi661 vs Galadriel). The opponent never casts and a land drop takes only lands, so with the floor
// its nonlands fall below two only to one of OUR effects (the USER's "the opponent needing to show that they can
// remove things"). The floor binds only in that case, so the opponent's land development is otherwise untouched
// (an unconditional lands-first shed measured as moving long games of nearly every deck: it discards the lands
// the opponent would have played). Off the rule, and for every deck without the lock trait: oldest first, as ever.
inline constexpr int kCleanupNonlandFloor = 2;
inline int CleanupShedIndex(const std::vector<Card>& hand, bool keep_floor)
{
    if (!keep_floor || hand.empty()) { return 0; }
    const int before = CountHandSides(hand).nonlands;
    const int need = std::min(kCleanupNonlandFloor, before);
    if (CountHandSides(hand, 0).nonlands >= need) { return 0; }
    for (int i = 1; i < static_cast<int>(hand.size()); ++i)
    { if (CountHandSides(hand, i).nonlands >= need) { return i; } }
    return 0;   // every shed costs a nonland (a hand of over seven nonlands): the floor cannot bind
}
inline void EndOfTurnCleanup(GameState& state)
{
    if (!state.opponent_library_dealt || state.opponent_decked) { return; }
    Player& opp = state.players[1];
    const bool keep_floor = FreeTurnHurdlesOn() && state.deck_has_free_turn;
    while (opp.hand.size() > 7)
    {
        const int at = CleanupShedIndex(opp.hand, keep_floor);
        opp.graveyard.push_back(opp.hand[static_cast<std::size_t>(at)]);
        opp.hand.erase(opp.hand.begin() + at);
    }
    // The second head discards the same way (into no modelled zone: nothing reads it).
    while (state.opponent2_hand.size() > 7)
    {
        const int at = CleanupShedIndex(state.opponent2_hand, keep_floor);
        state.opponent2_hand.erase(state.opponent2_hand.begin() + at);
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
