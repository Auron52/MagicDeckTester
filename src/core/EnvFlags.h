#pragma once
// THE env-flag truthiness convention -- one place, one rule (backlog item A3; see the
// coding-conventions skill). Before this header existed, three conventions coexisted
// (presence-only `getenv(...) != nullptr`, absence-only `== nullptr`, and value-aware),
// and for 88 flags `MTG_X=0` meant ON -- which is how MTG_MAGMA_FAITHFUL=0 once silently
// ENABLED the mode it looked like it was disabling, corrupting an A/B arm.
//
// The rule, everywhere:
//   unset or empty  -> the flag's DEFAULT (false unless the site passes dflt=true)
//   "0"             -> OFF
//   anything else   -> ON     ("1", "2", "trace", ...)
//
// So `MTG_X=0` always means off, `MTG_X=1` always means on, for every boolean flag.
// heuristic_defaults.env's "set the var to the baseline to disable" instruction is now
// correct for every flag.
//
// EnvSet() is NOT a truthiness reader: use it only for the "did the user pin a value?"
// pattern, where the same variable also carries a numeric/string value and 0 is a legal
// value (e.g. MTG_SHUFFLE_SALT_SEARCH=0 pins salt 0, which is different from unset).
#include <cstdlib>
#include <string>

// Boolean flag, value-aware. dflt is returned when the variable is unset or empty.
inline bool EnvOn(const char* key, bool dflt = false)
{
    const char* e = std::getenv(key);
    if (e == nullptr || *e == '\0') { return dflt; }
    return std::string(e) != "0";
}

// Raw presence -- ONLY for value-pinning detection (see header comment), never truthiness.
inline bool EnvSet(const char* key)
{
    return std::getenv(key) != nullptr;
}

// Integer knob: the variable's value, or dflt when unset/empty.
inline int EnvInt(const char* key, int dflt)
{
    const char* e = std::getenv(key);
    if (e == nullptr || *e == '\0') { return dflt; }
    return std::atoi(e);
}

// Portable setenv(). POSIX has setenv(3); MSVC has only _putenv_s, which ALWAYS overwrites --
// so the overwrite=false case must be guarded by hand there.
//
// overwrite=false is load-bearing, not a nicety: HeuristicDefaults applies the baked-in
// heuristic_defaults.env with overwrite=false precisely so a flag the USER set on the command
// line wins over the file. Silently overwriting would make the defaults file clobber an A/B
// arm's flag -- the same failure mode as the presence-only truthiness bug this header exists
// to prevent. Returns true if the variable now holds `value`.
inline bool EnvPut(const char* key, const char* value, bool overwrite)
{
    if (!overwrite && std::getenv(key) != nullptr) { return false; }
#ifdef _WIN32
    return _putenv_s(key, value) == 0;
#else
    return setenv(key, value, 1) == 0;
#endif
}
