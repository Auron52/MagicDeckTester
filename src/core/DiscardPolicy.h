#pragma once
#include <cstdlib>
#include <string>

// Scope of cleanup-discard protection for a deck's `required_pieces` (see
// SelectCleanupDiscardIndex in SpellEffects.h). Lives in its own header so both the low-level
// GameState and the AI-side MulliganProfile can name it without either including the other.
//
// The right scope is DECK-SPECIFIC, which is why it is a profile field rather than one global
// rule. Measured over the overnight suite (4 seeds, ~100k games/arm), LastInHand vs All:
//   dragonstorm  -0.0058  (Apex/Dragonstorm come in multiples; the survivors of an All-scope
//                          protection are the rituals that ARE the deck's mana, so it shed its
//                          engine and cast 0 spells -- three T8/T6/T5 wins became unwon)
//   antilife     -0.0009
//   th           +0.0036  (Treasure Hunt IS the engine; protecting duplicates is correct there)
//   burn          0.0000  (its pieces are 1-drops; the rule discards HIGHEST MV, so they were
//                          never candidates either way)
// Hence: default All (the established behaviour), opt into LastInHand per deck.
enum class DiscardProtectScope
{
    All,        // protect every copy of a required piece (default)
    LastInHand, // protect only while it is the sole copy in HAND -- a duplicate is spare
    LastInDeck, // protect only when no copy remains in hand OR library (truly irreplaceable)
};

inline DiscardProtectScope DiscardProtectScopeFromString(const std::string& s)
{
    if (s == "hand") { return DiscardProtectScope::LastInHand; }
    if (s == "deck") { return DiscardProtectScope::LastInDeck; }
    return DiscardProtectScope::All;
}

inline const char* DiscardProtectScopeToString(DiscardProtectScope s)
{
    switch (s)
    {
        case DiscardProtectScope::LastInHand: return "hand";
        case DiscardProtectScope::LastInDeck: return "deck";
        default:                              return "all";
    }
}

// A/B override: MTG_DISCARD_PROTECT=all|hand|deck forces the scope for EVERY deck, ignoring the
// profile. Unset (the normal case) -> the profile's per-deck value is used. Read once; the env
// cannot change mid-run, so this stays cheap on the discard path.
inline const char* DiscardProtectScopeOverride()
{
    static const char* const ov = std::getenv("MTG_DISCARD_PROTECT");
    return ov;
}
