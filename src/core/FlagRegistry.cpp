#include "FlagRegistry.h"
#include "../ai/HeuristicArm.h"
#include "flag_registry.h"   // generated: kKnownMtgFlags[], sorted

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>

extern char** environ;

namespace
{
    bool KnownFlag(const std::string& name)
    {
        return std::binary_search(std::begin(kKnownMtgFlags), std::end(kKnownMtgFlags),
                                  std::string_view(name),
                                  [](std::string_view a, std::string_view b) { return a < b; });
    }
}

void WarnUnknownMtgFlags()
{
    for (char** e = environ; *e; ++e)
    {
        if (std::strncmp(*e, "MTG_", 4) != 0) { continue; }
        const char* eq = std::strchr(*e, '=');
        const std::string name = eq ? std::string(*e, eq - *e) : std::string(*e);
        if (KnownFlag(name)) { continue; }
        std::fprintf(stderr,
                     "[flags] WARNING: %s is set but is not a flag this binary reads -- a typo or a"
                     " deleted flag? It will have NO effect (`mtg --list-flags` prints the known"
                     " set).\n",
                     name.c_str());
    }
}

void ValidateHeuristicArmNames()
{
    bool bad = false;
    for (int i = 0; i < heurarm::COUNT; ++i)
    {
        const char* n = heurarm::Name(i);
        if (n && KnownFlag(n)) { continue; }
        std::fprintf(stderr,
                     "[flags] FATAL: HeuristicArm slot %d names \"%s\", which no EnvOn() call site"
                     " reads. A per-job override of it would shadow nothing and the arm would"
                     " silently run the BASELINE.\n",
                     i, n ? n : "(null)");
        bad = true;
    }
    if (bad) { std::abort(); }
}

void PrintFlagRegistry(std::ostream& os)
{
    for (const char* f : kKnownMtgFlags) { os << f << '\n'; }
}
