#pragma once
// BLOOD SUN'S LAND-ABILITY INVENTORY (docs/design/blood-sun-pipeline.md 2.3 / 2.4) -- the ONE classification of every
// raw `parameters` key a LAND carries in cards.json, read by three consumers:
//   * CardDatabase's load (the mana-only projection: CardDefinition::land_ability_loss_twin / mana_only_identity drop
//     every Gated key; the self-guard field CardDefinition::land_ungated_ability names the first SelfGuard or
//     unclassified key);
//   * GoldFishRunner::StampDeckTraits (a Blood Sun list holding a land with such a key aborts at setup);
//   * test/unit/test_bloodsun_suppression.cpp, which pins the lists against cards.json: a land key that is in no list
//     (or in two) fails the test, so adding a land param without classifying it is caught there, not in a game.
//
// "All lands lose all abilities except mana abilities" (CR 613.1f, layer 6; a mana ability PRODUCES mana, CR 605.1a --
// one that only costs mana is not). The three classes:
//   Mana      -- part of a mana ability (what it makes, its cost, its restriction, painland damage that is part of the
//                ability) or an ability that does not work on the battlefield (cycling, a put-from-hand cost: land
//                cards not on the battlefield are unaffected, ruling 2). KEPT under Blood Sun.
//   Gated     -- a non-mana ability every chokepoint gates through LandNonManaAbilitiesSuppressed: enters-tapped
//                rules, "as enters" replacements, "when enters" triggers, the tap TRIGGERS (Forbidden Orchard, City of
//                Brass via tap_self_damage_is_trigger). DROPPED by the mana-only projection.
//   SelfGuard -- a non-mana ability with NO generic gate in this pass (activated non-mana abilities, statics,
//                upkeep triggers, as-enters choices whose mana ability then depends on the missing choice, counters a
//                mana ability must remove). A Blood Sun list holding such a land is refused at setup.
#include <string_view>

namespace landloss {

enum class LandKeyClass { Mana, Gated, SelfGuard, Unclassified };

inline constexpr std::string_view kManaKeys[] = {
    "produces", "produces_amount", "produces_one_color", "hand_fixing_produces",
    "is_filter", "any_color_filter", "ramp_filter", "reflecting",
    // painland / Ancient Tomb / Horizon-land damage is PART of the mana ability (City of Brass's is not:
    // tap_self_damage_is_trigger moves it to the Gated side, see StripsTapSelfDamage).
    "tap_self_damage", "tap_self_damage_any_mode", "tap_is_life_payment",
    "creature_mana_only", "mana_colors_of_controlled_subtype", "mana_colors_of_graveyard_legends",
    "mana_per_other_controlled_subtype", "mana_per_creature_feeder_generic", "mana_per_creature_subtype",
    "sac_for_mana_amount", "sac_for_mana_color", "sac_for_mana_victim_land_subtype",
    "energy_per_colored_tap", "tap_opponent_lifegain",
    "mdfc_back_name", "mdfc_back_produces",            // which face is played: the card, not a land ability
    "cycling_cost", "hand_put_self_cost",              // hand abilities (ruling 2)
};

inline constexpr std::string_view kGatedKeys[] = {
    // G1: the enters-tapped authority (LandWouldEnterTapped and its wrappers; Gond Gate's grant)
    "enters_tapped", "etb_pay_life_to_untap", "etb_untap_reveal_subtypes", "etb_untap_control_subtypes",
    "fastland_max_other_lands", "checkland_subtypes", "gates_enter_untapped",
    // G2/G3: the land-entry replacements and enter triggers (PlayLandCore, EnterLand, ResolveLandEnterEtbExtras)
    "etb_replace_sacrifice_land_subtype", "etb_replace_sacrifice_untapped_lands", "etb_sacrifice_lands",
    "etb_optional_tapped_rad", "etb_energy", "etb_sacrifice_unless_pay", "etb_scry", "etb_surveil",
    "etb_bounce_land", "etb_lifegain",
    // G5/G6: the tap TRIGGERS
    "taps_spawn_opp_token", "tap_self_damage_is_trigger",
};

inline constexpr std::string_view kSelfGuardKeys[] = {
    // activated, non-mana
    "fetch_land_types", "fetch_hold_human_only",
    "land_search_activation_cost", "land_search_activation_life_cost", "land_search_activation_sacrifice_self",
    "land_search_activation_taps", "land_search_allow_partial", "land_search_basic_only", "land_search_count",
    "land_search_share_land_type", "land_search_tapped",
    "sacrifice_draw_cost", "can_animate", "animate_cost", "animate_power", "animate_toughness",
    "gy_return_at_random", "gy_return_cost", "gy_return_requires_creature", "gy_return_requires_subtype",
    "tap_target_pump_cost", "tap_target_pump_grants_trample", "tap_target_pump_grants_unblockable",
    "tap_target_pump_per_subtype_both", "tap_target_pump_power", "tap_target_pump_tough",
    "tap_token_cost", "tap_token_power", "tap_token_requires_subtypes", "tap_token_subtypes", "tap_token_toughness",
    "tap_tutor_put_cost", "tap_tutor_put_return_self", "tutor_required_subtype", "tutor_shuffle_after", "tutor_types",
    "alt_win_distinct_named_count", "alt_win_distinct_named_subtype",
    "tap_treasure_cost", "tap_treasure_tap_other_subtype", "tap_damage_cost", "tap_damage_each_opponent",
    "tap_blood_cost", "tap_blood_cost_less_per_subtype",
    "team_pump_cost", "team_pump_power", "team_pump_subtypes", "team_pump_taps_source", "team_pump_tough",
    "untap_creature_cost", "untap_creature_subtype", "threshold_loot_cost", "self_bounce_cost",
    "sac_lifegain_per_creature", "sac_lifegain_per_creature_cost",
    "tap_draw_cost", "tap_draw_cost_less_per_rad", "tap_draw_requires_top_supertype", "tap_investigate_cost",
    "storage_charge_mode",
    // triggers and statics on the battlefield
    "upkeep_gy_reanimate", "upkeep_gy_reanimate_land_subtype", "upkeep_gy_reanimate_land_threshold",
    "upkeep_gy_reanimate_may", "storage_land", "own_creature_enters_lifegain", "enters_watch_subtypes",
    "no_max_hand_size", "all_nonbasic_land_types",
    // "as enters" choices / counters a MANA ability then depends on: dropping the entry leaves a mana ability the
    // engine would still model as working (no chosen colour / type, no counter to remove), so not gated here
    "etb_choose_color", "etb_choose_color_fixed", "chosen_color_adds_basic_land_type", "choose_type_on_enter",
    "colored_creature_only", "colored_creature_ability_ok",
    "enters_tapped_with_depletion", "enters_with_mana_counters", "mana_counters_are_mining",
    "sacrifice_when_mana_counters_empty",
};

inline LandKeyClass ClassifyLandKey(std::string_view k)
{
    for (std::string_view m : kManaKeys)      { if (m == k) { return LandKeyClass::Mana; } }
    for (std::string_view m : kGatedKeys)     { if (m == k) { return LandKeyClass::Gated; } }
    for (std::string_view m : kSelfGuardKeys) { if (m == k) { return LandKeyClass::SelfGuard; } }
    return LandKeyClass::Unclassified;
}

// The keys a TRIGGERED tap ping (tap_self_damage_is_trigger: City of Brass) moves to the Gated side.
inline bool StripsTapSelfDamage(std::string_view k)
{
    return k == "tap_self_damage" || k == "tap_self_damage_any_mode" || k == "tap_is_life_payment";
}

}  // namespace landloss
