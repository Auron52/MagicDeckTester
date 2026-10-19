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

// THE CONVOKE COVER of a madness cast (Vampires Stage 6a defect D1, 2026-10-19): WHAT the convoke bodies
// pay of the madness cost -- `pip` of its coloured pips of the spell's convoke colour (ConvokePipColor:
// Markov Baron's {B}) and `gen` of its generic -- packed into one byte (pip 0..7, gen 0..15). The reduced
// cost is the madness cost minus the cover, so it is a property of the PIN, never of the board: the search
// books exactly what the cast will pay. A card without creature convoke has the one cover (0, 0) == 0.
inline std::int8_t MadnessCoverPack(int pip, int gen)
{
    return static_cast<std::int8_t>((pip & 7) | ((gen & 15) << 3));
}
inline int MadnessCoverPip(std::int8_t arm) { return static_cast<int>(arm) & 7; }
inline int MadnessCoverGeneric(std::int8_t arm) { return (static_cast<int>(arm) >> 3) & 15; }

// One payable way to cast a pending card (MadnessOptionsNow / MadnessPinnedOptionNow). `cost_idx` indexes
// MadnessPending::costs; `arm` is the option's CONVOKE COVER (MadnessCoverPack -- 0 = no convoke, which is
// also the only cover of a card without it); `conv_pip` / `conv_other` are the bodies the cast taps on THIS
// board to pay that cover (of the spell's coloured pip / any other colour -- the least attacking power
// given up, free bodies first; CreatureConvokeArms); `reduced` is the mana actually paid after convoke.
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
//   choice  k = cast with costs[k-1], its convoke bodies paying the COVER `arm` (MadnessCoverPack).
// The pin names the cover -- what the bodies pay -- never an index into an arm list, and it is realised on
// the board AT THE TRIGGER by the cheapest body set that pays it there (MadnessPinnedOptionNow), so a body
// the plan adds before the trigger (a cast, a Vial put, a Bloodline Keeper token, an earlier madness cast)
// is used, and the mana the search reserved is exactly the mana the cast pays. (Defect D1, 2026-10-19:
// the pin used to name an arm INDEX into the PRE-PLAN arm list, which priced the arm on the pre-plan
// board and meant another arm -- or none, a decline -- on the board the trigger really saw.) A pinned
// cover that the trigger's board cannot pay (too few bodies, or the mana is not there) resolves as a
// DECLINE -- the stranded-premise rule (legal, never phantom mana).
struct MadnessPin
{
    std::int8_t choice = -1;
    std::int8_t arm    = 0;
};
