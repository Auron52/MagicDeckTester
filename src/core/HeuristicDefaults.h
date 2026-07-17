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
// BYTE-IDENTICAL when the file is absent or has no KEY=VALUE lines (no setenv calls -> nothing
// changes). Call ApplyHeuristicDefaults() at the very top of main(), before any code reads a toggle.
#include <cstdlib>
#include <fstream>
#include <string>
#include <filesystem>

inline void ApplyHeuristicDefaults(
    const std::filesystem::path& path = "src/ai/data/heuristic_defaults.env")
{
    std::ifstream f(path);
    if (!f) { return; }                       // absent -> no-op (byte-identical)
    auto trim = [](std::string s) {
        size_t b = s.find_first_not_of(" \t\r\n");
        size_t e = s.find_last_not_of(" \t\r\n");
        return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
    };
    std::string line;
    while (std::getline(f, line))
    {
        std::string t = trim(line);
        if (t.empty() || t[0] == '#') { continue; }     // blank / comment
        size_t eq = t.find('=');
        if (eq == std::string::npos) { continue; }      // malformed -> skip
        std::string key = trim(t.substr(0, eq));
        std::string val = trim(t.substr(eq + 1));
        if (key.empty()) { continue; }
        // overwrite = 0: an already-set env var wins (explicit override / disable / A-B).
        setenv(key.c_str(), val.c_str(), 0);
    }
}
