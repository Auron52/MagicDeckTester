#pragma once
#include <string>

// Tier classification for card implementations.
// Tiers 1 and 2 are data-driven (JSON); Tier 3 requires a C++ file in src/cards/custom/.
enum class CardTier
{
    Data,    // Tier 1: fully described by Scryfall data + template (no custom logic)
    Template, // Tier 2: parameterised template with a named C++ behaviour
    Custom   // Tier 3: dedicated C++ implementation in src/cards/custom/
};

// Parameterised behaviour templates for Tier 1 and Tier 2 cards.
// Each template maps to a handler registered in CardDatabase.
enum class CardTemplate
{
    None,            // Tier 3 — resolved by custom C++ code
    BasicLand,       // Tap: add one mana of a specific color
    VanillaCreature, // No abilities beyond combat stats
    ManaDork,        // Creature — Tap: add mana
    DirectDamage,    // Deal N damage to target creature or player
    CounterSpell,    // Counter target spell (optionally conditional)
    Removal,         // Destroy or exile a target permanent
    DrawSpell,       // Draw N cards (fixed N)
    DrawX,           // Draw X cards (X chosen on cast)
    PumpSpell,       // Target creature gets +N/+M until end of turn
    LordEffect,      // Creatures of a subtype get +N/+M
    Haste,           // Keyword-only creature (haste granted to others or self)
    DrawUntilNonland, // Reveal cards until a nonland; put all revealed (incl. nonland) into hand
};

// Convert template name string (from JSON) to enum.
CardTemplate CardTemplateFromString(const std::string& name);
const char*  CardTemplateToString(CardTemplate t);
