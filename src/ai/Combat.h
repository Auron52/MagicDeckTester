#pragma once
// THE combat core (backlog C1, unification unit 7): who attacks, and what their damage does.
// This existed twice -- GameEngine::CombatPhase (the real game) and TurnSolver's SimulateCombat
// (the rollout) -- as "same rules, separate code", which is the shape every historical lockstep bug
// in this engine has had. Both now share the two functions below.
//
// What is deliberately NOT shared, because it is genuinely executor-only: the combat STEPS
// (BeginCombat / DeclareAttackers / CombatDamage / EndCombat), stack resolution between them,
// state-based actions, the game log and the play-viewer event stream. The rollout has no stack and
// no logger, so those belong to GameEngine.
//
// Ordering note: each caller still computes its own attack-trigger tokens, self-pumps,
// firebreathing and exalted bonus before calling in, because the two do those in a different ORDER
// (the executor computes the exalted bonus after firebreathing, the rollout before). The values are
// identical today -- firebreathing only applies temporary power bonuses, it creates no permanents,
// so it cannot move CountExalted or the attacker count -- and passing the bonus in preserves each
// side's sequence exactly rather than silently picking one.
#include "../core/GameState.h"
#include <cstdint>
#include <string>
#include <vector>

// The active player's attacking creatures, as battlefield INDICES (stable across the token
// creation that follows, which only appends). Was a twin pair: AIEngine::DeclareAttackers returned
// pointers, SimulateCombat inlined the identical predicate.
std::vector<int> DeclareAttackerIndices(const GameState& state);

struct CombatDamageResult
{
    int total_damage      = 0;   // combat damage + attack-trigger life loss (the attack-log total)
    int trigger_life_loss = 0;
    // "<name> (<power>)" per attacker that dealt damage; filled only when collect_descs is set
    // (the executor's play-viewer history). Empty in the rollout.
    std::vector<std::string> attacker_descs;
};

// Deal every attacker's combat damage, apply lifelink, fire the attack triggers (Leeching Sliver's
// life loss, Utvara Hellkite's Dragon tokens, Goblin Lackey's cheat-into-play), and tap the
// non-vigilant attackers. `exalted_bonus` is the caller's already-computed Exalted bonus (nonzero
// only when exactly one creature attacks).
CombatDamageResult ResolveCombatDamage(GameState& state, const std::vector<int>& atk_idx,
                                       int exalted_bonus, bool collect_descs);

// ---- MTG_FB_HOLD_BANKED: the combat pump keeps BANKED mana unless the pump is lethal ----------------
//
// ApplyFirebreathing spends ALL leftover mana on attacker pumps, on the premise that combat is the
// turn's last mana use, so mana left unspent is lost. That premise is false for a BANKED source:
//   * a depletion land (Sandstone Needle, Peat Bog): each tap spends a counter, and the land is
//     sacrificed at zero -- the pump buys +2 damage with half a land;
//   * a storage land (Mercadian Bazaar, Dwarven Hold): the tap releases counters banked over turns,
//     and an untapped storage land banks +1 at end of turn.
// So a NON-lethal pump holds those sources back; a pump that makes the attack lethal still spends
// everything (the game ends). Lethality is judged on a COPY: the full pump with the caller's own
// payer, then the shared ResolveCombatDamage. The later declare-attackers triggers (Skyhunter dig,
// Flinger fling) are not replayed in the probe -- the conservative direction is only possible for a
// deck that holds both such a trigger and a banked land, and none does.
// dragonstorm d0 s1191687 T3 (the Needle sacrificed to a non-lethal Scourge pump; kill T7 vs the old
// T6) and s7600233 (the Bazaar's bank pumped away a turn before the storm needed it).

// The pump payer's type (the same alias SpellEffects.h declares; redeclaring an alias to the same type is
// well-formed, and it keeps this header off SpellEffects.h).
using FbPayer = bool (*)(GameState&, const ManaCost&);

// Bits (battlefield index < 64) of the active player's UNTAPPED banked sources.
std::uint64_t FbBankedSourceMask(const GameState& state);

// Should this combat's pump hold the banked sources? Lever on, a banked source untapped, and the full
// pump (with `payer`, on a copy) NOT lethal -- and (MTG_FB_HOLD_BANKED_NEXT_TURN) not the one pump that
// makes NEXT turn's attack lethal where holding does not. False (byte-identical) whenever no banked
// source is up.
//
// MTG_FB_HOLD_SEARCHED: that verdict ORDERS the two sides of a CONTESTED hold (a banked source up, the
// full pump not lethal) and the search picks one. `pin` is the searched rank the caller CONSUMED from
// GameState::scripted_fb_hold at this combat (-1 none / 0 the verdict / 1 the other side); 1 on a
// contested hold returns the opposite of the verdict, anything else returns the verdict. An uncontested
// combat ignores the pin (a lethal pump spends -- holding it is dominated; no banked source -> no hold).
bool FbHoldBankedThisCombat(const GameState& state, const std::vector<int>& atk_idx, FbPayer payer,
                            int pin = -1);

// MTG_FB_HOLD_STATS=1 (diagnostic, print-only, default off): where the searched banked hold's variants go.
// Slots: the axis fan (lists, variants), the pump sites' reads of a flip pin (unread: no attacker / no pump
// source; read: by outcome), and the unpinned contested combats for scale. Printed once at exit.
namespace fbholdstats
{
enum Slot { kFanLists, kFanVariants, kPinUnread, kPinRead, kPinNoBank, kPinLethal, kPinDominated, kPinFlipped,
            kBaseContested, kVariantFolded, kVariantScored, kCount };
bool On();
void Bump(Slot s);
}

// The hold is published per thread for the duration of one pump (both sites) ...
struct FbBankedHoldScope
{
    bool prev;
    explicit FbBankedHoldScope(bool on);
    ~FbBankedHoldScope();
    FbBankedHoldScope(const FbBankedHoldScope&)            = delete;
    FbBankedHoldScope& operator=(const FbBankedHoldScope&) = delete;
};

// ... and each pump PAYMENT installs it as a pay hold (recomputed per payment, so a pay-sac erase that
// shifts battlefield indices cannot misdirect it). A no-op unless an FbBankedHoldScope is on.
struct FbPumpPayHold
{
    std::uint64_t prev;
    explicit FbPumpPayHold(const GameState& s);
    ~FbPumpPayHold();
    FbPumpPayHold(const FbPumpPayHold&)            = delete;
    FbPumpPayHold& operator=(const FbPumpPayHold&) = delete;
};
