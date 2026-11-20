#pragma once
// BLOOD SUN: "All lands lose all abilities except mana abilities." (CR 613.1f, layer 6) -- the predicate and the
// effective-definition helpers every land chokepoint reads (docs/design/blood-sun-pipeline.md 2.2-2.3).
//
// THE SHAPE. A land's non-mana abilities are cards.json params; cards/LandAbilityClasses.h classifies each one as a
// mana ability (kept), GATED (dropped while a Blood Sun is on the battlefield) or SELF-GUARDED (a Blood Sun list
// holding such a land is refused at setup). CardDatabase builds each land's MANA-ONLY PROJECTION at load
// (CardDefinition::land_ability_loss_twin: the same card with every gated param at its default), so a chokepoint
// that reads a land's params through EffectiveLandDef sees "enters untapped, bounces nothing, scries nothing, City of
// Brass pings nothing" under Blood Sun with no per-param edit, and the ORIGINAL definition everywhere else.
//
// Timing (rulings 3-4, CR 614.12): a land entering while Blood Sun is on the battlefield has already lost its
// "as enters" / replacement / "when enters" abilities, so every entry site judges the state AS THE LAND ENTERS.
// Nothing is retroactive: a land that entered tapped stays tapped, a resolved bounce stays resolved.
//
// Gated per game by GameState::deck_has_land_ability_loss (StampDeckTraits), so every deck without a Blood Sun pays
// one bool test and is byte-identical.
#include "GameState.h"
#include "../cards/CardDatabase.h"

// True while a permanent with lands_lose_nonmana_abilities is on the battlefield (either controller: "All lands").
// The battlefield is SCANNED rather than counted: it runs only under the stamp, the battlefield is small, and a
// counter would need every leave path kept exact (design 2.2: replace with a counter if profiling ever shows it).
inline bool LandNonManaAbilitiesSuppressed(const GameState& s)
{
    if (!s.deck_has_land_ability_loss) { return false; }
    for (const Permanent& p : s.battlefield)
    {
        if (p.def_absent || p.card.IsLand()) { continue; }
        const CardDefinition* d = CardDatabase::Instance().LookupCached(p.card);
        if (d != nullptr && d->params.lands_lose_nonmana_abilities) { return true; }
    }
    return false;
}

// The definition a land's abilities are read from in `s`: its mana-only projection while Blood Sun suppresses land
// abilities, else `def` itself (also for a non-land, a land with nothing to drop, and a Register-built definition).
inline const CardDefinition& EffectiveLandDef(const GameState& s, const CardDefinition& def)
{
    if (def.land_ability_loss_twin == nullptr || !LandNonManaAbilitiesSuppressed(s)) { return def; }
    return *def.land_ability_loss_twin;
}
inline const CardDefinition* EffectiveLandDef(const GameState& s, const CardDefinition* def)
{
    return def == nullptr ? nullptr : &EffectiveLandDef(s, *def);
}
// The same, for a caller that has already evaluated the predicate once (a loop over many lands).
inline const CardDefinition& EffectiveLandDefIf(bool suppressed, const CardDefinition& def)
{
    return (suppressed && def.land_ability_loss_twin != nullptr) ? *def.land_ability_loss_twin : def;
}

// G9: the identity two lands are interchangeable by in `s` -- the mana-only projection's while suppressed (Lotus
// Vale == Lotus Field under Blood Sun), else behaviour_identity. 0 = no identity (a Register-built definition).
inline std::uint64_t EffectiveLandIdentity(const GameState& s, const CardDefinition& def)
{
    if (def.mana_only_identity != 0 && def.card.IsLand() && LandNonManaAbilitiesSuppressed(s))
    { return def.mana_only_identity; }
    return def.behaviour_identity;
}

// G5: the life a tap of this source costs its controller, by the land's tap_self_damage. City of Brass's ping is a
// TRIGGERED ability ("Whenever this land becomes tapped", CardParams::tap_self_damage_is_trigger), lost under Blood
// Sun; a painland's / Ancient Tomb's / a Horizon land's is part of the mana ability and stays. Every life-deducting
// tap site and the pain pricing that gates on "does this tap hurt" read this; the walk runs only for a land that
// carries the trigger param, so every other source is the plain field read.
inline int EffectiveTapSelfDamage(const GameState& s, const CardDefinition& def)
{
    const int dmg = def.params.tap_self_damage;
    if (dmg <= 0 || !def.params.tap_self_damage_is_trigger) { return dmg; }
    return LandNonManaAbilitiesSuppressed(s) ? 0 : dmg;
}
