#pragma once
#include "../core/Card.h"
#include "CardTemplate.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <vector>

// What a spell or ability can legally target.
// Used by the AI to check for legal targets before casting,
// and by CastSpellFromHand to build the correct Target list.
enum class Targeting
{
    None,     // no target required (creatures, draw spells, etc.)
    Any,      // any target: player, planeswalker, or creature (e.g. Lightning Bolt)
    Player,   // players/planeswalkers only
    Creature, // creatures only (e.g. Searing Blood)
    Multi,    // one player target AND one creature that player controls (e.g. Searing Blaze)
};

// Parameters extracted from a card's JSON definition, forwarded to the template handler.
struct CardParams
{
    int damage       = 0;
    int draw         = 0;
    int power_bonus  = 0;
    int tough_bonus  = 0;
    Targeting targeting = Targeting::None;
    bool sacrifice_land = false;              // additional cost: sacrifice a land (e.g. Shard Volley)
    std::optional<ManaCost> spectacle_cost;  // alternate cost when opponent lost life this turn
    std::vector<Color> produces;   // mana colors this card produces
    std::vector<std::string> subtypes_affected;  // for lord effects
};

// A fully resolved card definition: base Card data plus template + parameters.
struct CardDefinition
{
    Card card;
    CardTier tier        = CardTier::Data;
    CardTemplate tmpl    = CardTemplate::None;
    CardParams params;
};

// Singleton registry of all known card definitions.
// Populated from:
//   - JSON files in src/cards/data/   (Tiers 1 & 2)
//   - Registration calls in custom card files (Tier 3)
class CardDatabase
{
public:
    static CardDatabase& Instance();

    // Load all card definitions from a JSON file.
    // Can be called multiple times to load multiple files.
    void LoadFromJson(const std::filesystem::path& path);

    // Register a Tier 3 custom card. Called from generated registration functions.
    using CardFactory = std::function<CardDefinition()>;
    void Register(const std::string& name, CardFactory factory);

    // Look up a card by name (case-sensitive, matches Scryfall name).
    std::optional<CardDefinition> Lookup(const std::string& name) const;

    bool IsImplemented(const std::string& name) const;

    // Returns all registered card names — used by the analyzer to check coverage.
    std::vector<std::string> AllNames() const;

private:
    CardDatabase() = default;

    Card BuildCardFromJson(const nlohmann::json& entry) const;
    CardParams BuildParamsFromJson(const nlohmann::json& params) const;

    std::unordered_map<std::string, CardDefinition> m_cards;
};
