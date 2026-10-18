#pragma once
// MADNESS (CR 702.35) -- the plain data types of the madness core (Vampires A1, 2026-10-18;
// docs/design/vampires/design_A1_madness.md §2.3). Kept in their own header because two layers
// need them: the shared resolver in core/SpellEffects.h, and the DecisionProvider hook
// (MadnessCastDefault) that SpellEffects.h itself includes through ai/DecisionProviders.h.
#include "Card.h"
#include <cstdint>
#include <string>
#include <vector>

// One discarded card waiting for its madness trigger. The card is in EXILE while pending (CR 702.35a).
// `costs` are the DISTINCT madness costs it had AT THE DISCARD (cheapest first): its own madness_cost
// and/or Falkenrath Gorger's granted "its mana cost" -- SNAPSHOTTED, so a Gorger that leaves the
// battlefield between the discard and the trigger's resolution does not revoke the grant (2016-04-08
// ruling). The queue is CALLER-LOCAL: every madness trigger resolves inside the same function that
// discarded, so no GameState field holds it (sizeof(GameState) does not move).
struct MadnessPending
{
    Card                  card;
    int                   owner = 0;
    std::vector<ManaCost> costs;
    std::string           cause;   // what discarded it ("Stromkirk Condemned", "cleanup", ...)
};

// When the trigger resolves -- decides which convoke arms exist (after combat and at cleanup every
// untapped creature is a FREE convoke body: it can no longer attack, and the passive opponent never
// attacks, so it has no other use) and which provider rule applies at depth 0.
enum class MadnessWindow : std::uint8_t { PreCombatMain, PostCombatMain, Cleanup, Resolution };

// One payable way to cast a pending card (MadnessOptionsNow). `cost_idx` indexes
// MadnessPending::costs; `arm` is the convoke arm (0 = every FREE body that helps, or no convoke for a
// card without it; a >= 1 = the free bodies plus `a` COSTLY ones, lowest power first); `conv_pip` /
// `conv_other` are the bodies that arm taps (of the spell's coloured pip / any other colour);
// `reduced` is the mana actually paid after convoke.
struct MadnessOption
{
    std::int8_t cost_idx   = 0;
    std::int8_t arm        = 0;
    std::int8_t conv_pip   = 0;
    std::int8_t conv_other = 0;
    ManaCost    reduced;
};

// The SEARCHED answer carried by an outlet Action (Action::madness_choice / madness_arm):
//   choice -1 = unpinned (the provider's MadnessCastDefault: beyond the horizon, the depth-0 runner,
//              cleanup, or the human chooser's preselect),
//   choice  0 = decline (the card goes to the graveyard),
//   choice  k = cast with costs[k-1] using convoke arm `arm`.
// The pin names the arm, not raw tap counts, so a plan whose earlier casts ADD free bodies still
// realises "every free body"; a pinned option that no longer exists (or is no longer payable) at the
// trigger resolves as a DECLINE -- the stranded-premise rule (legal, never phantom mana).
struct MadnessPin
{
    std::int8_t choice = -1;
    std::int8_t arm    = 0;
};
