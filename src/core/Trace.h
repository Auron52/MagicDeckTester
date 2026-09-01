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
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

namespace trace
{
    // Format one whole line into a caller-owned buffer and emit it with a SINGLE stderr write.
    //
    // WHY ONE WRITE. This used to be three calls -- prefix, body, newline -- and every runner in
    // this repo is multi-threaded, so concurrent tracers interleaved MID-LINE and produced lines
    // with other lines spliced into them. That is not cosmetic: any tool parsing a trace stream
    // silently gets torn records, and a behavioural diff built on them compares decisions that were
    // never made. One locked write per line makes a line atomic against other lines.
    //
    // Truncation (a line longer than the buffer) is marked, not hidden, so a silently clipped
    // record can never be mistaken for a complete one.
    inline void Emit(const char* stream, const char* fmt, ...)
#if defined(__GNUC__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;

    inline void Emit(const char* stream, const char* fmt, ...)
    {
        static const char kMark[] = "...[TRUNCATED]";
        // Room for the marker and the newline is reserved up front, so appending either can never
        // itself overflow -- the case that would turn a truncation guard into the bug it guards.
        char  buf[1024];
        const std::size_t cap = sizeof(buf) - sizeof(kMark) - 1;   // usable text, marker, '\n'
        bool  cut = false;
        std::size_t off = 0;

        const int n = std::snprintf(buf, cap + 1, "[%s] ", stream);
        if (n < 0) { return; }
        if (static_cast<std::size_t>(n) > cap) { off = cap; cut = true; }
        else                                   { off = static_cast<std::size_t>(n); }

        if (!cut)
        {
            std::va_list ap;
            va_start(ap, fmt);
            const int m = std::vsnprintf(buf + off, cap + 1 - off, fmt, ap);
            va_end(ap);
            if (m < 0) { return; }
            if (static_cast<std::size_t>(m) > cap - off) { off = cap; cut = true; }
            else                                         { off += static_cast<std::size_t>(m); }
        }

        if (cut)
        {
            std::memcpy(buf + off, kMark, sizeof(kMark) - 1);
            off += sizeof(kMark) - 1;
        }
        buf[off++] = '\n';
        std::fwrite(buf, 1, off, stderr);
    }
}

// Print one trace line to stderr if `stream` is enabled. The per-call-site enable
// flag is cached after the first check, so a disabled stream costs ~one bool load.
#define TRACE(stream, ...)                                              \
    do {                                                                \
        static const bool _trace_on = ::trace::StreamEnabled(stream);   \
        if (_trace_on) { ::trace::Emit(stream, __VA_ARGS__); }          \
    } while (0)
