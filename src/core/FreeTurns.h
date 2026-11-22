#pragma once
// OPPONENT-LOCKDOWN FREE TURNS (USER proposal + rulings 2026-11-21; docs/design/opponent-lockdown-free-turns.md).
// MTG_FREE_TURNS (DEFAULT ON since 2026-11-22) arms two things, both defined here so the two worlds and the deal read ONE
// definition:
//
//   1. THE LOCK CHECK (LockHolds / NoteOpponentTurnEnd), at the END of each passive-opponent regular turn,
//      in BOTH worlds (GameEngine::RunTurnFrom, TurnSolver's SimulateEndAndStartNextTurn -- lockstep). The
//      opponent is LOCKED when EVERY opposing head (2HG: both, each on its own zones -- USER) has
//      (no land on the battlefield) OR (no nonland card in hand; an MDFC counts as the side the hand has fewer
//      of, CountHandSides), AND no creature is on the opposing side (the shared board) -- counting a scheduled
//      spawn due with our next turn ONLY when it passes the spawn land requirement (SpawnMayAppear), since
//      that body is the opponent's for the turn that just ended. The decision reads the board as it stands at
//      the END of the opponent's turn (USER 2026-11-22). A locked turn raises GameState::pending_free_turn and the
//      next AdvanceTurnCounter keeps the ROUND (turn_number) where it is: a FREE TURN, accounted like an
//      extra turn (USER 2026-10-26: rounds are the unit of every win turn and window).
//
//   2. THE OPENING-HAND HURDLES (ApplyOpeningHurdles, called right after opponentdeck::Deal), so mana screw
//      or flood cannot trip the lock by itself (USER: "a real hurdle even with mana screw"). They REPLACE,
//      never add (USER: "it would keep the 1 land a turn rhythm"; hand stays 7, library untouched):
//        * SCREW: n < 2 lands (CountHandSides) -> the 2 - n RIGHTMOST nonlands become BASICS, each
//          in the slot it replaces; the displaced cards leave the game.
//        * FLOOD: m < 2 nonlands -> 2 - m lands (the RIGHTMOST -- USER-confirmed 2026-11-22) leave the game and
//          uncastable placeholders take the RIGHT end of the hand (the leftmost-first cleanup sheds real cards
//          first).
//        * BOUNCELAND EXTENSION (USER): afterwards, if NO land in the hand can stay on an empty battlefield (all
//          Karoos / Lake of the Dead), the RIGHTMOST of them becomes a basic.
//      The basic is the list's most common basic (a real basic card; ties -> the first in list order); a list
//      with none gets the basic of the colour its lands produce most (copies x colours, ties WUBRG).
//      MTG_FREE_TURN_HURDLES=0 measures the credit without them.
//
//   3. THE SPAWN LAND REQUIREMENT (SpawnNeedsLand; USER 2026-11-22): a scheduled opponent creature spawn appears
//      only while the opponent controls a land. Own flag, MTG_OPP_SPAWN_NEEDS_LAND (EngineFlags.h).
//
// EVERYTHING IS GATED ON THE DECK'S LOCK TRAIT (RuleArmed: MTG_FREE_TURNS AND GameState::deck_has_free_turn =
// GoldFishRunner::DeckMayLockOpponent; USER 2026-11-22: "if we do it right, there won't be any change to any of
// our existing decks since none lock the board like this"). For a deck without the trait the credit, the hurdles,
// the cleanup floor and the spawn requirement are all inert, so it plays byte-identically to the flag off. The
// trait list must therefore be COMPLETE: a deck it misses gets no free turn at all.
// With MTG_FREE_TURNS off none of it runs: pending_free_turn is never raised, so every game is byte-identical.
#include "GameState.h"
#include "OpponentDeck.h"
#include "SpellEffects.h"   // LandFaceDefOf / PlayableAsLand: the hand rule for an MDFC's land face
#include "GameLogger.h"
#include "../ai/EngineFlags.h"
#include "../cards/CardDatabase.h"
#include <array>
#include <atomic>
#include <cstdio>
#include <string>
#include <vector>

namespace freeturn {

// The placeholder nonland the flood hurdle deals (registered by CardDatabase: a Sorcery nobody can cast).
inline constexpr const char* kPlaceholderName = "Opponent Placeholder Spell";
// Numbers for hurdle cards, inside each head's band (HeadOfNumber) and clear of the 60-card library.
inline constexpr int kHurdleBasicOffset       = 9000;
inline constexpr int kHurdlePlaceholderOffset = 9100;

// ---- the gate ---------------------------------------------------------------------------------------------

// The rule acts for THIS game: the flag is on and the deck carries the lock trait (stamped per game by
// GoldFishRunner::StampDeckTraits before the deal, so the hurdles can read it).
inline bool RuleArmed(const GameState& state) { return FreeTurnsOn() && state.deck_has_free_turn; }

// ---- the spawn land requirement --------------------------------------------------------------------------

// Does the requirement act in this game? Only on the real-opponent model (a dealt list that plays lands; a
// legacy-replay board never has a land). MTG_OPP_SPAWN_NEEDS_LAND unset -> exactly where the rule is armed.
inline bool SpawnNeedsLand(const GameState& state)
{
    if (!state.opponent_plays_lands || g_play_legacy_opponent_spawn_any) { return false; }
    const int env = OppSpawnNeedsLandEnv();
    return env >= 0 ? env != 0 : RuleArmed(state);
}
// The opponent side controls a land (either head: the spawns go to the shared opposing board). The spawn point is
// the start of OUR turn, so "a land played this turn" by the opponent is one it still controls from its last
// turn; this test covers both halves of the USER's "no lands played or on board".
inline bool OpponentControlsLand(const GameState& state)
{
    for (const Permanent& p : state.battlefield)
    { if (p.controller_index != 0 && p.card.IsLand()) { return true; } }
    return false;
}
// Would a scheduled spawn appear now? Read by both worlds' spawn sites (GameEngine::UntapStep, TurnSolver's
// SimulateEndAndStartNextTurn; lockstep) and by the lock. Nothing changes the opponent's lands between the end of
// its turn and our untap, so the lock's reading and the spawn site's agree.
// The on-the-draw turn-1 spawn needs no special case: on the draw the opponent's turn 0 precedes ours and plays the
// leftmost land of its hand (GameEngine::RunTurnFrom), so at our first untap it controls a land whenever its
// hand held one it could play. A turn-1 spawn is refused only when the opening hand held none (Burn gi434: a 0-land 7) (fourth pass: the deleted
// MTG_OPP_SPAWN_DRAW_T1 lever measured byte-identical in every block of 6,300 trait-deck games).
inline bool SpawnMayAppear(const GameState& state)
{
    return !SpawnNeedsLand(state) || OpponentControlsLand(state);
}

// ---- the lock --------------------------------------------------------------------------------------------

// One head's half of condition 1: it controls no land (its card-number band; tokens included -- a land is a
// land) OR its hand holds no nonland card (opponentdeck::CountHandSides: an MDFC counts as the side the hand has
// fewer of, so it fills an empty side -- {Mountain, Turntimber Symbiosis} holds a nonland; a lone MDFC is a nonland,
// the tie, so an opponent with a land out and a lone MDFC is NOT locked: it would cast the spell side).
inline bool HeadLocked(const GameState& state, int head)
{
    bool land = false;
    for (const Permanent& p : state.battlefield)
    {
        if (opponentdeck::HeadControls(p, head) && p.card.IsLand()) { land = true; break; }
    }
    if (!land) { return true; }
    const std::vector<Card>& hand = head == 2 ? state.opponent2_hand : state.players[1].hand;
    return opponentdeck::CountHandSides(hand).nonlands == 0;
}

// Condition 2, on the shared board: no creature controlled by the opponent side, and no scheduled spawn due with
// our next regular turn that will appear (SpawnMayAppear; the schedule keys on the RAW round, GameState::RawRound).
// Such a spawn is the body the opponent fielded in the turn that just ended; one the land requirement stops never
// exists, so it cannot block the lock.
inline bool NoOpposingCreature(const GameState& state)
{
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != 0 && (p.card.IsCreature() || p.is_animated)) { return false; }
    }
    if (state.opponent_spawns != nullptr && SpawnMayAppear(state))
    {
        const int next_raw = state.RawRound() + 1;
        for (const OpponentSpawn& s : *state.opponent_spawns) { if (s.turn == next_raw) { return false; } }
    }
    return true;
}

// SIRE OF INSANITY, the fairness check (MTG_FREE_TURN_SIRE_FAIR, DEFAULT OFF; USER 2026-11-22: "The reality is,
// though, that it can lock out some decks and cannot for others."). Sire empties the opponent's hand at EVERY end
// step, ours included, so the hand it meets at the opponent's end step is exactly what the opponent drew or kept
// that turn. If that hand holds a CREATURE card the opponent could have cast with the lands on its battlefield --
// mana value <= its land count and each coloured pip covered by that many lands producing the colour (an X is 0;
// hybrid pips count their first colour, as ManaCost stores them) -- a real opponent would have deployed it, and
// the creature would break the lock: that turn's lock does not hold. A CHECK only; the game state is unchanged.
// Head 1 only: Sire's discard reaches the dealt hand of head 1 (PerformEndStepDiscardHands), not head 2's.
inline bool SireDeployableCreature(const GameState& state)
{
    bool sire = false;
    for (const Permanent& p : state.battlefield)
    {
        if (p.controller_index != 0 || p.def_absent) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d != nullptr && d->params.endstep_each_player_discards_hand) { sire = true; break; }
    }
    if (!sire) { return false; }
    const opponentdeck::HeadMana m = opponentdeck::HeadManaOf(state, 1);
    for (const Card& c : state.players[1].hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (d != nullptr && d->card.IsCreature() && opponentdeck::CastableWith(d, m)) { return true; }
    }
    return false;
}
// Both worlds, in the opponent's turn right BEFORE Sire's end-step discard (GameEngine::RunTurnFrom, TurnSolver's
// SimulateEndAndStartNextTurn; lockstep). Assigned every opponent turn, so an opponent extra turn's value never
// reaches the regular turn's check.
inline void NoteSireBeforeDiscard(GameState& state)
{
    if (!RuleArmed(state) || !FreeTurnSireFairOn() || !state.deck_has_demon_triggers) { return; }
    state.sire_lock_veto = SireDeployableCreature(state);
}

// The whole rule. Only the real-opponent model qualifies (a dealt list that plays lands); a legacy replay's
// three-placeholder board never locks.
inline bool LockHolds(const GameState& state)
{
    if (!state.opponent_plays_lands || state.opponent_decked || OpponentHasLost(state)) { return false; }
    if (state.sire_lock_veto) { return false; }   // MTG_FREE_TURN_SIRE_FAIR: it would have deployed a creature
    if (state.annihilator_lock_veto) { return false; }   // MTG_FREE_TURN_ANNIHILATOR_FAIR: it would have kept a permanent
    if (!HeadLocked(state, 1)) { return false; }
    if (opponentdeck::HasSecondHead(state) && !HeadLocked(state, 2)) { return false; }
    return NoOpposingCreature(state);
}

// Called by both worlds right after the opponent's REGULAR turn (the one before our next regular turn; not one
// before our extra turn, which the opponent does not get). Raises pending_free_turn when the lock holds.
inline void NoteOpponentTurnEnd(GameState& state)
{
    if (!RuleArmed(state)) { return; }
    state.pending_free_turn = LockHolds(state);
    state.sire_lock_veto = false;
    state.annihilator_lock_veto = false;
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

// ---- the round + 1 stand-down, narrowed to a REACHABLE lock (MTG_FREE_TURN_REACH) -------------------------------
//
// A lock-trait deck's next turn may share this round (a free turn), so the turn-edge prunes that assume "the next turn
// is round + 1" stand down for it (NextTurnMayShareRound below; TurnSolver's edge folds, edge tail elision, winless
// certificate, FSL edge no-reach, go-off dominance). Standing down where NO free turn can follow is wasted search:
// with the one-land removals in the trait (USER 2b) Sea Creatures paid +26% search units, Prevent Damage and Landfall
// Elementals more, for zero free turns. MTG_FREE_TURN_REACH (DEFAULT ON; =0 stands down for every node of a trait
// deck, as before) stands down only where the lock is REACHABLE from the node.
//
// THE CERTIFICATE (sound under the engine's opponent model; checked by MTG_FREE_TURN_REACH_AUDIT, below). It applies
// only to a deck whose cards cannot touch the opponent's hand or library (GameState::deck_free_turn_hand_intact,
// GoldFishRunner::DeckLockNeedsLandStrip: no Worldfire, no Sire of Insanity, no annihilator, no opponent-target loot,
// no mill / forced draw), i.e. the decks whose only lock cards are single-target land removals. For those the
// opponent's hand changes only by its own draws (appended at the RIGHT end), its land drop and its cleanup. A head
// holding, in hand,
//   (a) a "sure land": a real land (front face) that stays alone (not a Karoo, not Lake of the Dead) and is not a
//       fetchland, with no fetchland to its LEFT -- the drop policy (opponentdeck::OpponentLandDropIndex) takes the
//       leftmost non-wasted real land, and every real non-fetch land it could take instead leaves a land behind
//       (a Karoo is taken only with another land in play and keeps itself; a Lake only with a Swamp, which it
//       replaces; a legendary copy is "wasted" only while its twin is in play) -- and
//   (b) a plain nonland (not an MDFC; the flood hurdle's placeholder counts, as in CountHandSides),
// ends its coming turn controlling a land AND holding a nonland, whatever the rest of OUR turn does: our removals
// act on the battlefield, which its land drop refills, and its cleanup never sheds the last nonland while it holds
// lands (CleanupShedIndex sheds the MORE COMMON kind). Such a head is not locked (HeadLocked), and the lock needs
// EVERY head, so no free turn can follow this turn. A board with no real opponent never locks (LockHolds).
// The narrowing removes no line at any budget: it skips exactly the stand-downs a free turn cannot use.
inline bool FreeTurnReachOn()
{
    static const bool on = EnvOn("MTG_FREE_TURN_REACH", true);   // DEFAULT ON; =0 -> stand down for every node
    return heurarm::Flag(heurarm::FREE_TURN_REACH, on);           // per-job arm (one pooled A/B batch)
}
inline bool OpponentFetchLand(const CardDefinition* def)
{
    return def != nullptr && (!def->params.fetch_land_types.empty() || def->params.land_search_activation_sacrifice_self);
}
// Condition (a) AND (b) for one head's hand.
inline bool HeadHoldsSureLandAndNonland(const std::vector<Card>& hand)
{
    bool fetch_left = false, sure_land = false, nonland = false;
    for (const Card& c : hand)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(c);
        if (opponentdeck::IsSpellLandMdfc(d)) { continue; }   // either side: no claim from it
        if (!opponentdeck::HandCardIsLandFront(c)) { nonland = true; continue; }
        if (d == nullptr) { return false; }   // an unknown land: no claim
        if (OpponentFetchLand(d)) { fetch_left = true; continue; }
        if (!fetch_left && LandStaysAlone(d)) { sure_land = true; }
    }
    return sure_land && nonland;
}
// May a free turn follow the turn being searched at this node? TRUE (stand down) unless the certificate holds.
inline bool FreeTurnMayFollow(const GameState& state)
{
    if (!state.deck_free_turn_hand_intact || !FreeTurnReachOn()) { return true; }
    if (!state.opponent_plays_lands || state.opponent_decked) { return false; }   // LockHolds is false for good
    if (HeadHoldsSureLandAndNonland(state.players[1].hand)) { return false; }
    if (opponentdeck::HasSecondHead(state) && HeadHoldsSureLandAndNonland(state.opponent2_hand)) { return false; }
    return true;
}
// The turn-edge prunes' gate: a following turn may share this turn's ROUND -- an extra turn, or a free turn that is
// reachable from here. Identical to deck_has_extra_turn for every deck without the lock trait.
inline bool NextTurnMayShareRound(const GameState& state)
{
    return state.deck_has_extra_turn || (state.deck_has_free_turn && FreeTurnMayFollow(state));
}

// MTG_FREE_TURN_REACH_AUDIT=1 (MEASUREMENT ONLY; no behaviour): both worlds evaluate the certificate on the state
// that ENDS our turn (right before the opponent's regular turn) and, after its lock check, count every turn the
// certificate called unlockable that locked anyway (UNSOUND, must be 0). The executor also checks the certificate's
// other half, that OUR turn left the opponent's hand alone: the hand at the end of our turn must extend the hand at
// its start (only appends; GameEngine::RunTurnFrom). Printed at exit.
struct ReachAudit
{
    std::atomic<long long> checks{0}, certified{0}, unsound{0}, hand_checks{0}, hand_touched{0};
    ~ReachAudit()
    {
        if (checks.load() == 0 && hand_checks.load() == 0) { return; }
        std::fprintf(stderr, "[free-turn-reach] checks=%lld certified=%lld UNSOUND=%lld (must be 0) "
                     "hand_checks=%lld hand_touched=%lld (must be 0)\n", checks.load(), certified.load(),
                     unsound.load(), hand_checks.load(), hand_touched.load());
    }
};
inline ReachAudit g_reach_audit;
inline bool ReachAuditOn() { static const bool on = EnvOn("MTG_FREE_TURN_REACH_AUDIT"); return on; }
// -1 = not audited; 1 = the certificate holds (no free turn may follow); 0 = it does not.
inline int ReachAuditBefore(const GameState& state)
{
    if (!ReachAuditOn() || !state.deck_free_turn_hand_intact || !RuleArmed(state)) { return -1; }
    g_reach_audit.checks.fetch_add(1, std::memory_order_relaxed);
    const bool cert = !FreeTurnMayFollow(state);
    if (cert) { g_reach_audit.certified.fetch_add(1, std::memory_order_relaxed); }
    return cert ? 1 : 0;
}
inline void ReachAuditAfter(const GameState& state, int before)
{
    if (before == 1 && state.pending_free_turn)
    {
        g_reach_audit.unsound.fetch_add(1, std::memory_order_relaxed);
        std::fprintf(stderr, "[free-turn-reach] UNSOUND: seed=%llu round=%d locked after a certified turn\n",
                     static_cast<unsigned long long>(state.game_seed), state.turn_number);
    }
}
inline std::vector<int> ReachAuditHandAtTurnStart(const GameState& state)
{
    std::vector<int> v;
    if (!ReachAuditOn() || !state.deck_free_turn_hand_intact) { return v; }
    for (const Card& c : state.players[1].hand) { v.push_back(c.m_number); }
    v.push_back(-1);
    for (const Card& c : state.opponent2_hand) { v.push_back(c.m_number); }
    return v;
}
inline void ReachAuditHandAtTurnEnd(const GameState& state, const std::vector<int>& start)
{
    if (start.empty()) { return; }
    g_reach_audit.hand_checks.fetch_add(1, std::memory_order_relaxed);
    auto extends = [](const std::vector<Card>& hand, const int* first, const int* last) {
        const std::size_t n = static_cast<std::size_t>(last - first);
        if (hand.size() < n) { return false; }
        for (std::size_t i = 0; i < n; ++i) { if (hand[i].m_number != first[i]) { return false; } }
        return true;
    };
    const int* b = start.data();
    const int* mid = b;
    while (*mid != -1) { ++mid; }
    const int* e = b + start.size();
    if (!extends(state.players[1].hand, b, mid) || !extends(state.opponent2_hand, mid + 1, e))
    {
        g_reach_audit.hand_touched.fetch_add(1, std::memory_order_relaxed);
        std::fprintf(stderr, "[free-turn-reach] HAND TOUCHED: seed=%llu round=%d\n",
                     static_cast<unsigned long long>(state.game_seed), state.turn_number);
    }
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

// THE BOUNCELAND OPENING RULE (the hurdles' third step; also every game under MTG_OPP_BOUNCE_OPENING -- USER
// 2026-11-22: "we shouldn't be playing a bounceland first if we don't have an existing land unless forced by what the
// player does. The basic replacing bounceland rule should apply."). If the hand holds a land and NO land in it can
// stay on an empty battlefield (all Karoos / Lake of the Dead), the RIGHTMOST of them becomes a basic, in its slot,
// numbered `next_basic` (advanced). Returns the number of basics dealt (0 or 1).
inline int ApplyBouncelandOpening(std::vector<Card>& hand, const std::string& basic_name, int& next_basic)
{
    int rightmost_land = -1;
    bool any_stays = false;
    for (int i = 0; i < static_cast<int>(hand.size()); ++i)
    {
        const CardDefinition* d = CardDatabase::Instance().LookupCached(hand[static_cast<std::size_t>(i)]);
        if (!PlayableAsLand(d)) { continue; }
        rightmost_land = i;
        if (LandStaysAlone(d)) { any_stays = true; }
    }
    if (rightmost_land < 0 || any_stays) { return 0; }
    Card c;
    c.m_name = basic_name;
    c.RehashName();
    c.m_number = next_basic++;
    hand[static_cast<std::size_t>(rightmost_land)] = c;
    return 1;
}

// What the hurdles did to one hand (unit tests, the measurement's diagnostics).
struct HurdleResult { int screw_basics = 0; int flood_placeholders = 0; int bounce_basics = 0; };

// Apply the three hurdles to one head's opening hand. `band` is the head's number base (kNumberBase /
// kNumberBase2), so a hurdle card belongs to its head (HeadOfNumber).
// Lands / nonlands are CountHandSides' (an MDFC counts as the side the hand has fewer of; USER 2026-11-22), recounted
// after every swap. A swap takes only a card that is SURELY on its side: the screw replaces the rightmost
// non-MDFC nonland, the flood removes the rightmost non-MDFC land (USER-confirmed: the RIGHTMOST lands).
inline HurdleResult ApplyHurdlesToHand(std::vector<Card>& hand, const std::string& basic_name, int band)
{
    HurdleResult r;
    auto mdfc = [](const Card& c) { return opponentdeck::IsSpellLandMdfc(CardDatabase::Instance().LookupCached(c)); };
    auto plain_land = [&](const Card& c) { return !mdfc(c) && opponentdeck::HandCardIsLandFront(c); };
    auto plain_nonland = [&](const Card& c) { return !mdfc(c) && !opponentdeck::HandCardIsLandFront(c); };
    auto make = [](const std::string& name, int number) {
        Card c;
        c.m_name = name;
        c.RehashName();
        c.m_number = number;
        return c;
    };
    int next_basic = band + kHurdleBasicOffset;
    // SCREW: the rightmost nonlands become basics, in place.
    while (opponentdeck::CountHandSides(hand).lands < 2)
    {
        int at = -1;
        for (int i = static_cast<int>(hand.size()) - 1; i >= 0; --i)
        { if (plain_nonland(hand[static_cast<std::size_t>(i)])) { at = i; break; } }
        if (at < 0) { break; }
        hand[static_cast<std::size_t>(at)] = make(basic_name, next_basic++);
        ++r.screw_basics;
    }
    // FLOOD: the rightmost lands leave; placeholders go to the right end.
    int next_ph = band + kHurdlePlaceholderOffset;
    while (opponentdeck::CountHandSides(hand).nonlands < 2)
    {
        int at = -1;
        for (int i = static_cast<int>(hand.size()) - 1; i >= 0; --i)
        { if (plain_land(hand[static_cast<std::size_t>(i)])) { at = i; break; } }
        if (at < 0) { break; }
        hand.erase(hand.begin() + at);
        hand.push_back(make(kPlaceholderName, next_ph++));
        ++r.flood_placeholders;
    }
    // BOUNCELAND EXTENSION: no land that can stay alone -> the rightmost land becomes a basic.
    r.bounce_basics = ApplyBouncelandOpening(hand, basic_name, next_basic);
    return r;
}

// After opponentdeck::Deal (GoldFishRunner::SetupGame, the scenario loader): both heads' opening hands.
// Without the armed rule, MTG_OPP_BOUNCE_OPENING (DEFAULT ON) applies the bounceland opening rule alone, in every game.
inline void ApplyOpeningHurdles(GameState& state)
{
    if (!state.opponent_plays_lands || !state.opponent_library_dealt) { return; }
    if (!FreeTurnHurdlesOn() || !RuleArmed(state))
    {
        if (!OppOpeningBounceBasicOn()) { return; }
        int b1 = opponentdeck::kNumberBase + kHurdleBasicOffset;
        ApplyBouncelandOpening(state.players[1].hand, HurdleBasicName(state.opponent_slot), b1);
        if (opponentdeck::HasSecondHead(state))
        {
            int b2 = opponentdeck::kNumberBase2 + kHurdleBasicOffset;
            ApplyBouncelandOpening(state.opponent2_hand, HurdleBasicName(state.opponent2_slot), b2);
        }
        return;
    }
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
