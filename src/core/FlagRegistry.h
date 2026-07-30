#pragma once
// Startup MTG_* env-flag validation + the --list-flags dump (backlog A3 step 4 / D1).
//
// The flag surface is env-var based, so a typo'd (MTG_BP_WAVE for MTG_BP_WAVES) or deleted flag
// is a SILENT no-op -- worst case it corrupts an A/B arm that believes the lever is set. The
// registry of known names is generated at build time from the source itself
// (cmake/FlagRegistry.cmake), so it cannot drift.
#include <ostream>

// Scan the process environment for MTG_* vars not in the generated registry and print one
// prominent stderr WARNING per unknown name. Deliberately non-fatal: hard-failing would break
// every older script that still exports a since-deleted flag; the warning makes the no-op
// visible in the run's log instead. Call once, early in main(), AFTER ApplyHeuristicDefaults()
// (its setenv'd defaults are registry-whitelisted, but order keeps the environment final).
void WarnUnknownMtgFlags();

// Print every known MTG_* flag name, one per line (the registry the validator checks against).
void PrintFlagRegistry(std::ostream& os);
