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

    // On-cast trigger: when the controller casts a spell with MV <= on_cast_trigger_max_mv,
    // deal on_cast_trigger_damage to that player. Used for Eidolon of the Great Revel.
    int on_cast_trigger_max_mv  = 0;
    int on_cast_trigger_damage  = 0;

    // Landfall: if > 0 and a land entered the battlefield under the caster's control
    // this turn, use this value instead of `damage` (e.g. Searing Blaze).
    int  landfall_damage = 0;

    // If true, drawn cards are placed in the staged exile zone rather than the hand
    // and expire at the end of the player's next turn (e.g. Light Up the Stage).
    bool stages_cards = false;

    // Death trigger: if > 0 and the targeted creature dies from this spell's damage,
    // deal this much damage to its controller (e.g. Searing Blood).
    int death_trigger_damage = 0;

    // Lord scaling: if true, the P/T bonus scales per other matching creature on the
    // battlefield rather than being a flat bonus (e.g. Predatory Sliver).
    bool scales_per_matching = false;

    // Attack trigger: deal this much damage to the opponent per attacker that matches
    // subtypes_affected (e.g. Leeching Sliver: 1 per attacking Sliver).
    int attack_trigger_damage = 0;

    // Keyword lords: grant the named keyword to all creatures matching subtypes_affected.
    bool grants_haste        = false;  // Cloudshredder Sliver, Thrumming Hivepool
    bool grants_double_strike = false; // Thrumming Hivepool

    // Affinity for subtype: reduce this card's generic mana cost by 1 per matching
    // permanent you control (e.g. Thrumming Hivepool — Affinity for Slivers).
    bool affinity_for_subtype = false;

    // Aether Vial: if true, this permanent gains a charge counter each upkeep (with
    // AI heuristic to stop at the optimal count), and can tap to put a creature from
    // hand with MV equal to the counter count onto the battlefield.
    bool upkeep_adds_charge = false;

    // Animated land (e.g. Mutavault): if true, the AI may pay animate_cost during the
    // main phase to make this permanent a creature with animate_power/animate_toughness
    // and all creature types until end of turn.
    bool                   can_animate      = false;
    int                    animate_power    = 0;
    int                    animate_toughness = 0;
    std::optional<ManaCost> animate_cost;

    // Replicate: if true, this permanent itself has replicate (cost = its own mana cost),
    // creating a token copy for each additional time the cost is paid at cast time.
    bool has_replicate = false;

    // Replicate lord: if true, Sliver spells (or matching subtypes_affected) you cast
    // have replicate (e.g. Hatchery Sliver). Checked on permanents already in play when
    // a new matching creature is cast.
    bool grants_replicate_to_subtypes = false;

    // Creature-only mana: if true, mana from this land may only be spent to cast creature
    // spells (e.g. Ancient Ziggurat). Enforced at payment time and in solver pool checks.
    bool creature_mana_only = false;

    // Upkeep token creation: at the beginning of upkeep, create N creature tokens
    // with the given power/toughness/subtypes (e.g. Thrumming Hivepool: 2 × 1/1 Sliver).
    int upkeep_creates_tokens     = 0;
    int upkeep_token_power        = 0;
    int upkeep_token_toughness    = 0;
    std::vector<std::string> upkeep_token_subtypes;

    // Tap-and-pay activated token creation: {tap_token_cost}, {T} creates 1 token
    // with the given power/toughness/subtypes. Only activatable when at least one
    // permanent matching tap_token_requires_subtypes is controlled (e.g. Sliver Hive).
    std::optional<ManaCost>  tap_token_cost;
    int                      tap_token_power     = 0;
    int                      tap_token_toughness = 0;
    std::vector<std::string> tap_token_subtypes;
    std::vector<std::string> tap_token_requires_subtypes;

    // Enters tapped (e.g. Saprazzan Skerry, Lonely Sandbar, Temple of Epiphany).
    // If true, the permanent is placed on the battlefield tapped and cannot
    // produce mana until the next turn's untap step.
    bool enters_tapped = false;

    // No maximum hand size (e.g. Reliquary Tower). If true, the cleanup-step discard
    // to 7 is skipped while this permanent is on the battlefield.
    bool no_max_hand_size = false;

    // Land's Edge pattern: "Discard a land card: deal this much damage to target player."
    // When > 0 this permanent provides the ability; AI will discard all hand lands for damage.
    int  discard_land_damage = 0;

    // Cascade: when cast, exile from library top until a nonland card with mana value
    // strictly less than cascade_max_mv is found; cast it for free; put the rest on the bottom.
    // 0 = no cascade.
    int  cascade_max_mv = 0;
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
