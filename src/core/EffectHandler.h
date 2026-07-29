#pragma once
#include "GameState.h"
#include "../cards/CardDatabase.h"

// Dispatches resolved stack entries to their template-specific effect handlers.
// Called by GameEngine::ResolveStack after popping each entry.
class EffectHandler
{
public:
    // Execute the effect of a resolved stack entry.
    // Returns false if the spell fizzled (all targets became illegal).
    static bool Resolve(GameState& state, const StackEntry& entry, const CardDefinition& def);

private:
    // The per-template dispatch. Resolve() wraps it so the legend rule -- a STATE-BASED action --
    // runs as soon as the permanent has entered and its ETB effects have run, for EVERY template,
    // rather than being repeated in each resolver (and forgotten in one). See Resolve().
    static bool ResolveImpl(GameState& state, const StackEntry& entry, const CardDefinition& def);

    static void EnterBattlefield(GameState& state, const StackEntry& entry, const CardDefinition& def);
    static void MoveToGraveyard(GameState& state, const StackEntry& entry);

    static void ResolveVanillaCreature(GameState& state, const StackEntry& entry, const CardDefinition& def);
    static void ResolveManaDork(GameState& state, const StackEntry& entry, const CardDefinition& def);
    static void ResolveDirectDamage(GameState& state, const StackEntry& entry, const CardDefinition& def);
    static void ResolveCounterSpell(GameState& state, const StackEntry& entry, const CardDefinition& def);
    static void ResolveRemoval(GameState& state, const StackEntry& entry, const CardDefinition& def);
    static void ResolveDrawSpell(GameState& state, const StackEntry& entry, const CardDefinition& def);
    static void ResolveDrawX(GameState& state, const StackEntry& entry, const CardDefinition& def);
    static void ResolvePumpSpell(GameState& state, const StackEntry& entry, const CardDefinition& def);
    static void ResolveDrawUntilNonland(GameState& state, const StackEntry& entry, const CardDefinition& def);
    static void ResolveCascade(GameState& state, const StackEntry& entry, const CardDefinition& def);
};
