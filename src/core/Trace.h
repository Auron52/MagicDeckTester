#pragma once
// ---------------------------------------------------------------------------
// Toggleable trace-stream logging.
//
// A tiny, dependency-free logging toolkit for leaving diagnostic tracing in the
// code permanently without it ever printing in normal runs. Each trace call names
// a STREAM (e.g. "turn", "search", "fetch"); a stream prints only when it is
// listed in the MTG_TRACE environment variable. Examples:
//
//     MTG_TRACE=all              ./mtg ...      # every stream
//     MTG_TRACE=turn,search      ./mtg ...      # just those two
//     (MTG_TRACE unset/empty)                   # nothing -- the normal case
//
// Output goes to stderr (so it never pollutes --batch stdout parsing or game logs).
// When a stream is disabled the only cost per call site is one cached bool test, so
// trace calls are safe to leave in hot paths. Use:
//
//     TRACE("search", "depth=%d plans=%zu", depth, plans.size());
//     if (TRACE_ON("search")) { ... expensive-to-format detail ... }
//
// Streams are matched exactly against the comma-separated MTG_TRACE list (or "all").
// Parsing happens once (function-local static); thereafter each TRACE_ON is a set
// lookup, and each call site caches its own enable flag.
// ---------------------------------------------------------------------------
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>

namespace trace
{
    // Parse MTG_TRACE once into the set of enabled stream names (or {"all"}).
    inline const std::set<std::string>& EnabledStreams()
    {
        static const std::set<std::string> streams = []
        {
            std::set<std::string> s;
            const char* env = std::getenv("MTG_TRACE");
            if (env && *env)
            {
                std::string cur;
                for (const char* p = env; ; ++p)
                {
                    if (*p == ',' || *p == '\0')
                    {
                        if (!cur.empty()) { s.insert(cur); }
                        cur.clear();
                        if (*p == '\0') { break; }
                    }
                    else { cur += *p; }
                }
            }
            return s;
        }();
        return streams;
    }

    inline bool StreamEnabled(const char* name)
    {
        const std::set<std::string>& s = EnabledStreams();
        if (s.empty()) { return false; }
        static const bool all = s.count("all") > 0;
        return all || s.count(name) > 0;
    }
}

// True if `stream` is enabled -- guard expensive detail formatting with this.
#define TRACE_ON(stream) (::trace::StreamEnabled(stream))

// Print one trace line to stderr if `stream` is enabled. The per-call-site enable
// flag is cached after the first check, so a disabled stream costs ~one bool load.
#define TRACE(stream, ...)                                              \
    do {                                                                \
        static const bool _trace_on = ::trace::StreamEnabled(stream);   \
        if (_trace_on) {                                                \
            std::fprintf(stderr, "[%s] ", stream);                      \
            std::fprintf(stderr, __VA_ARGS__);                          \
            std::fputc('\n', stderr);                                   \
        }                                                               \
    } while (0)
