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
