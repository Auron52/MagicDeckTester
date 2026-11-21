#pragma once
// OPPONENT-LOCKDOWN FREE TURNS (USER proposal + rulings 2026-11-21; docs/design/opponent-lockdown-free-turns.md).
// MTG_FREE_TURNS (DEFAULT OFF) arms two things, both defined here so the two worlds and the deal read ONE
// definition:
//
//   1. THE LOCK CHECK (LockHolds / NoteOpponentTurnEnd), at the END of each passive-opponent regular turn,
//      in BOTH worlds (GameEngine::RunTurnFrom, TurnSolver's SimulateEndAndStartNextTurn -- lockstep). The
//      opponent is LOCKED when EVERY opposing head (2HG: both, each on its own zones -- USER) has
//      (no land on the battlefield) OR (no nonland card in hand), AND no creature is on the opposing side
//      (the shared board) -- counting a scheduled spawn due with our next turn, since that body is the
//      opponent's for the turn that just ended. A locked turn raises GameState::pending_free_turn and the
//      next AdvanceTurnCounter keeps the ROUND (turn_number) where it is: a FREE TURN, accounted like an
//      extra turn (USER 2026-10-26: rounds are the unit of every win turn and window).
//
//   2. THE OPENING-HAND HURDLES (ApplyOpeningHurdles, called right after opponentdeck::Deal), so mana screw
//      or flood cannot trip the lock by itself (USER: "a real hurdle even with mana screw"). They REPLACE,
//      never add (USER: "it would keep the 1 land a turn rhythm"; hand stays 7, library untouched):
//        * SCREW: n < 2 lands (an MDFC's land face counts) -> the 2 - n RIGHTMOST nonlands become BASICS, each
//          in the slot it replaces; the displaced cards leave the game.
//        * FLOOD: m < 2 nonlands -> 2 - m lands (the RIGHTMOST -- an agent default, reported) leave the game and
//          uncastable placeholders take the RIGHT end of the hand (the leftmost-first cleanup sheds real cards
//          first).
//        * BOUNCELAND EXTENSION (USER): afterwards, if NO land in the hand can stay on an empty battlefield (all
//          Karoos / Lake of the Dead), the RIGHTMOST of them becomes a basic.
//      The basic is the list's most common basic (a real basic card; ties -> the first in list order); a list
//      with none gets the basic of the colour its lands produce most (copies x colours, ties WUBRG).
//      MTG_FREE_TURN_HURDLES=0 measures the credit without them.
//
// With MTG_FREE_TURNS off neither runs: pending_free_turn is never raised, so every game is byte-identical.
#include "GameState.h"
#include "OpponentDeck.h"
#include "SpellEffects.h"   // LandFaceDefOf / PlayableAsLand: the hand rule for an MDFC's land face
#include "GameLogger.h"
#include "../ai/EngineFlags.h"
#include "../cards/CardDatabase.h"
#include <array>
#include <string>
#include <vector>

namespace freeturn {

// The placeholder nonland the flood hurdle deals (registered by CardDatabase: a Sorcery nobody can cast).
inline constexpr const char* kPlaceholderName = "Opponent Placeholder Spell";
// Numbers for hurdle cards, inside each head's band (HeadOfNumber) and clear of the 60-card library.
inline constexpr int kHurdleBasicOffset       = 9000;
inline constexpr int kHurdlePlaceholderOffset = 9100;

// ---- the lock --------------------------------------------------------------------------------------------

// A card in an opponent's hand that is NOT a land card (CR: in hand a spell//land MDFC has only its front face,
// so Turntimber Symbiosis is a nonland card there). An unknown name counts as a nonland.
inline bool IsNonlandHandCard(const Card& c)
{
    const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
    return d == nullptr ? !c.IsLand() : !d->card.IsLand();
}

// One head's half of condition 1: it controls no land (its card-number band; tokens included -- a land is a
// land) OR its hand holds no nonland card.
inline bool HeadLocked(const GameState& state, int head)
{
    bool land = false;
    for (const Permanent& p : state.battlefield)
    {
        if (opponentdeck::HeadControls(p, head) && p.card.IsLand()) { land = true; break; }
    }
    if (!land) { return true; }
    const std::vector<Card>& hand = head == 2 ? state.opponent2_hand : state.players[1].hand;
    for (const Card& c : hand) { if (IsNonlandHandCard(c)) { return false; } }
    return true;
}

// Condition 2, on the shared board: no creature controlled by the opponent side, and no scheduled spawn due with
// our next regular turn (the spawn schedule keys on the RAW round, GameState::RawRound).
inline bool NoOpposingCreature(const GameState& state)
{
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != 0 && (p.card.IsCreature() || p.is_animated)) { return false; }
    }
    if (state.opponent_spawns != nullptr)
    {
        const int next_raw = state.RawRound() + 1;
        for (const OpponentSpawn& s : *state.opponent_spawns) { if (s.turn == next_raw) { return false; } }
    }
    return true;
}

// The whole rule. Only the real-opponent model qualifies (a dealt list that plays lands); a legacy replay's
// three-placeholder board never locks.
inline bool LockHolds(const GameState& state)
{
    if (!state.opponent_plays_lands || state.opponent_decked || OpponentHasLost(state)) { return false; }
    if (!HeadLocked(state, 1)) { return false; }
    if (opponentdeck::HasSecondHead(state) && !HeadLocked(state, 2)) { return false; }
    return NoOpposingCreature(state);
}

// Called by both worlds right after the opponent's REGULAR turn (the one before our next regular turn; not one
// before our extra turn, which the opponent does not get). Raises pending_free_turn when the lock holds.
inline void NoteOpponentTurnEnd(GameState& state)
{
    if (!FreeTurnsOn()) { return; }
    state.pending_free_turn = LockHolds(state);
}

// ---- the hurdles -----------------------------------------------------------------------------------------

// A land card whose land face can stay on an empty battlefield: not a Karoo (it would return itself) and not
// Lake of the Dead (no Swamp to sacrifice, so it goes to the graveyard).
inline bool LandStaysAlone(const CardDefinition* def)
{
    const CardDefinition* face = LandFaceDefOf(def);
    if (face == nullptr) { return false; }
    return !face->params.etb_bounce_land && face->params.etb_replace_sacrifice_land_subtype.empty();
}

// The hurdle basic for a pool slot (cached per slot; computed once).
inline const std::string& HurdleBasicName(std::size_t slot)
{
    static const std::vector<std::string> kNames = [] {
        std::vector<std::string> out;
        static const char* kByColour[5] = { "Plains", "Island", "Swamp", "Mountain", "Forest" };
        for (const opponentdeck::DeckSpec& spec : opponentdeck::Pool())
        {
            std::string best;
            int best_n = 0;
            std::array<int, 5> colour{};
            for (const std::pair<const char*, int>& e : spec.cards)
            {
                const CardDefinition* d = CardDatabase::Instance().Lookup(std::string(e.first));
                if (d == nullptr || !d->card.IsLand()) { continue; }
                if (d->card.HasSupertype(Supertype::Basic))
                {
                    if (e.second > best_n) { best_n = e.second; best = e.first; }
                    continue;
                }
                for (Color c : d->params.produces)
                {
                    const int k = static_cast<int>(c);
                    if (k >= 0 && k < 5) { colour[static_cast<std::size_t>(k)] += e.second; }
                }
            }
            if (best.empty())
            {
                int k_best = -1;
                for (int k = 0; k < 5; ++k)
                {
                    if (colour[static_cast<std::size_t>(k)] > (k_best < 0 ? 0 : colour[static_cast<std::size_t>(k_best)]))
                    { k_best = k; }
                }
                best = k_best >= 0 ? kByColour[k_best] : "Wastes";
            }
            out.push_back(best);
        }
        return out;
    }();
    static const std::string kNone = "Wastes";
    return slot < kNames.size() ? kNames[slot] : kNone;
}

// What the hurdles did to one hand (unit tests, the measurement's diagnostics).
struct HurdleResult { int screw_basics = 0; int flood_placeholders = 0; int bounce_basics = 0; };

// Apply the three hurdles to one head's opening hand. `band` is the head's number base (kNumberBase /
// kNumberBase2), so a hurdle card belongs to its head (HeadOfNumber).
inline HurdleResult ApplyHurdlesToHand(std::vector<Card>& hand, const std::string& basic_name, int band)
{
    HurdleResult r;
    auto is_land = [](const Card& c) { return PlayableAsLand(CardDatabase::Instance().LookupCached(c)); };
    auto make = [](const std::string& name, int number) {
        Card c;
        c.m_name = name;
        c.RehashName();
        c.m_number = number;
        return c;
    };
    int lands = 0;
    for (const Card& c : hand) { if (is_land(c)) { ++lands; } }
    const int nonlands = static_cast<int>(hand.size()) - lands;
    int next_basic = band + kHurdleBasicOffset;
    // SCREW: the rightmost nonlands become basics, in place.
    for (int need = 2 - lands; need > 0; --need)
    {
        int at = -1;
        for (int i = static_cast<int>(hand.size()) - 1; i >= 0; --i)
        { if (!is_land(hand[static_cast<std::size_t>(i)])) { at = i; break; } }
        if (at < 0) { break; }
        hand[static_cast<std::size_t>(at)] = make(basic_name, next_basic++);
        ++r.screw_basics;
    }
    // FLOOD: the rightmost lands leave; placeholders go to the right end.
    int next_ph = band + kHurdlePlaceholderOffset;
    for (int need = 2 - nonlands; need > 0; --need)
    {
        int at = -1;
        for (int i = static_cast<int>(hand.size()) - 1; i >= 0; --i)
        { if (is_land(hand[static_cast<std::size_t>(i)])) { at = i; break; } }
        if (at < 0) { break; }
        hand.erase(hand.begin() + at);
        hand.push_back(make(kPlaceholderName, next_ph++));
        ++r.flood_placeholders;
    }
    // BOUNCELAND EXTENSION: no land that can stay alone -> the rightmost land becomes a basic.
    int rightmost_land = -1;
    bool any_stays = false;
    for (int i = 0; i < static_cast<int>(hand.size()); ++i)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(hand[static_cast<std::size_t>(i)]);
        if (!PlayableAsLand(d)) { continue; }
        rightmost_land = i;
        if (LandStaysAlone(d)) { any_stays = true; }
    }
    if (rightmost_land >= 0 && !any_stays)
    {
        hand[static_cast<std::size_t>(rightmost_land)] = make(basic_name, next_basic++);
        ++r.bounce_basics;
    }
    return r;
}

// After opponentdeck::Deal (GoldFishRunner::SetupGame, the scenario loader): both heads' opening hands.
inline void ApplyOpeningHurdles(GameState& state)
{
    if (!FreeTurnHurdlesOn() || !state.opponent_plays_lands || !state.opponent_library_dealt) { return; }
    ApplyHurdlesToHand(state.players[1].hand, HurdleBasicName(state.opponent_slot), opponentdeck::kNumberBase);
    if (opponentdeck::HasSecondHead(state))
    {
        ApplyHurdlesToHand(state.opponent2_hand, HurdleBasicName(state.opponent2_slot), opponentdeck::kNumberBase2);
    }
}

// The play event at the start of a free turn (real play only; EmitPlayEvent is a no-op without a sink).
inline void EmitFreeTurnEvent(const GameState& state)
{
    if (!state.in_free_turn) { return; }
    EmitPlayEvent(state.turn_number, "free_turn",
                  "Free turn: the opponent ended its turn locked (turn " + std::to_string(state.turn_number)
                  + " again, free " + std::to_string(static_cast<int>(state.free_turns_taken)) + ")");
}

} // namespace freeturn
