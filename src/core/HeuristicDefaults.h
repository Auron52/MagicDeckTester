#pragma once
// Committed heuristic DEFAULTS loader (workstream 5 -- the auto_heuristics winner-activation step).
//
// auto_heuristics.py adopts a measurably-better heuristic variant AUTONOMOUSLY (adopt-then-review:
// the user vetoes after the fact). To make an adoption the LIVE default without a rebuild -- and
// keep it one-toggle reversible -- the winning value is written as a KEY=VALUE line into the
// committed data file src/ai/data/heuristic_defaults.env. At startup the engine applies each line
// as the DEFAULT for getenv(KEY), but ONLY if KEY is not already set in the environment, so an
// explicit `MTG_X=...` still overrides it. That override is exactly the disable / A-B lever:
//   * disable an adopted heuristic:  MTG_X=<baseline>   (or delete its line from the file)
//   * A/B a candidate against the current default:  MTG_X=<variant>
//
// BYTE-IDENTICAL when the file is absent or has no KEY=VALUE lines (no EnvPut calls -> nothing
// changes). Call ApplyHeuristicDefaults() at the very top of main(), before any code reads a toggle.
#include "core/EnvFlags.h"          // EnvPut -- portable setenv (MSVC has only _putenv_s)
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>
#include <filesystem>

// Resolve the defaults file INDEPENDENT of the process CWD. The old behavior resolved the
// relative path against the CWD only, so running the binary from logs/, a batch worker, or any
// non-root directory silently dropped every adopted default -- with no warning and no fingerprint
// difference to catch it (a cross-machine-reproducibility hazard). Now: walk UP from the
// executable's own directory (build/<Config>/mtg -> repo root two levels up, but the walk handles
// any nesting) looking for the relative path; fall back to the CWD-relative path if not found.
inline std::filesystem::path ResolveHeuristicDefaultsPath(const std::filesystem::path& rel)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path exe = fs::read_symlink("/proc/self/exe", ec);   // Linux; other platforms fall through
    if (!ec)
    {
        for (fs::path dir = exe.parent_path(); !dir.empty(); dir = dir.parent_path())
        {
            fs::path cand = dir / rel;
            if (fs::exists(cand, ec) && !ec) { return cand; }
            if (dir == dir.root_path()) { break; }
        }
    }
    return rel;                                              // fallback: CWD-relative (old behavior)
}

inline void ApplyHeuristicDefaults(
    const std::filesystem::path& path = "src/ai/data/heuristic_defaults.env")
{
    const std::filesystem::path resolved =
        path.is_absolute() ? path : ResolveHeuristicDefaultsPath(path);
    std::ifstream f(resolved);
    if (!f) { return; }                       // absent -> no-op (byte-identical)
    auto trim = [](std::string s) {
        size_t b = s.find_first_not_of(" \t\r\n");
        size_t e = s.find_last_not_of(" \t\r\n");
        return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
    };
    std::string line;
    std::string applied;                      // "KEY=VAL KEY2(env-override) ..."
    int n_lines = 0;
    while (std::getline(f, line))
    {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') { continue; }     // blank / comment
        size_t eq = t.find('=');
        if (eq == std::string::npos) { continue; }      // malformed -> skip
        std::string key = trim(t.substr(0, eq));
        std::string val = trim(t.substr(eq + 1));
        if (key.empty()) { continue; }
        const bool overridden = std::getenv(key.c_str()) != nullptr;
        // overwrite = false: an already-set env var wins (explicit override / disable / A-B).
        EnvPut(key.c_str(), val.c_str(), /*overwrite=*/false);
        ++n_lines;
        if (!applied.empty()) { applied += ' '; }
        applied += overridden ? key + "(env-override)" : key + "=" + val;
    }
    // Make the live adopted-defaults set visible in the run's own log. Silent when the file has
    // no KEY=VALUE lines (the byte-identical stock state) so normal runs emit nothing.
    if (n_lines > 0)
    {
        std::cerr << "[heuristic-defaults] " << resolved.string() << ": " << applied << "\n";
    }
}
