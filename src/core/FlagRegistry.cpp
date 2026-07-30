#include "FlagRegistry.h"
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

void PrintFlagRegistry(std::ostream& os)
{
    for (const char* f : kKnownMtgFlags) { os << f << '\n'; }
}
