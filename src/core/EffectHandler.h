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

public:
    // ---- Real-stack trigger machinery (BreachingDragonstorm onboarding, 2026-09-03) ----
    // Push a spell's CAST triggers (cascade instances, demonstrate) onto the stack ABOVE the
    // just-pushed spell entry, so they resolve first (CR 601.2i / 603.3b). Called by
    // AIEngine::CastSpellFromHand and by PushFreeCast, so nested free-cast chains fire their
    // own triggers. No-op for every card without a cast-trigger param -> byte-identical.
    static void PushCastTriggers(GameState& state, const CardDefinition& def, int controller);

    // A free cast is a REAL cast (CR 601.2): it increments spells_cast_this_turn, spends the
    // Irencrag budget, fires on-cast triggers/prowess, and pushes the spell's own cast
    // triggers. Returns false if a cast restriction forbids it (caller decides the card's
    // fallback zone). Used by cascade / Breaching Dragonstorm / Creative Technique resolution.
    static bool PushFreeCast(GameState& state, const Card& card, int controller);

private:
    // Triggered-entry resolution (dispatched from Resolve() on EntryType::Triggered).
    static bool ResolveTriggered(GameState& state, const StackEntry& entry, const CardDefinition& def);
    static void ResolveCascadeTrigger(GameState& state, const StackEntry& entry, const CardDefinition& def);
    static void ResolveEtbExileFreeCast(GameState& state, const StackEntry& entry, const CardDefinition& def);
    static void ResolveDemonstrate(GameState& state, const StackEntry& entry, const CardDefinition& def);
    // Creative Technique's payload (shuffle -> reveal until nonland -> exile it -> bottom the
    // rest -> may free-cast). Runs for the original spell AND its demonstrate copy.
    static void ResolveShuffleRevealFreecast(GameState& state, const StackEntry& entry, const CardDefinition& def);
};
