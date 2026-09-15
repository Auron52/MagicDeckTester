#pragma once
// Process-wide MEMORY BUDGET for the result-neutral search caches, plus an RSS watchdog.
//
// WHY THIS EXISTS (2026-09-15). The engine's three big memos -- the transposition table
// (MTG_TT_CAP), the flicker-search line cache (MTG_FSL_POOL / MTG_FSL_CAP) and the whole-plan
// caches (MTG_PLAN_CACHE_KB) -- are result-neutral (a refused store just recomputes; see each
// reader's own comment for the byte-identity measurements) but every one of them defaulted to
// UNBOUNDED. Only the generation drivers bounded them (scripts/lib/membudget.sh, sourced by
// valueleaf.sh / mullgen.sh, mirrored in analyze_deck.py); the regression harness, ref_bench, the
// deck-average scripts and the combo-off sweeps ran with every cache unlimited. One EDF reference
// game at a 500 ms virtual budget grew to 30.5 GB anon RSS that way and the kernel OOM-killed the
// pooled batch around it -- on a host whose RAM is shared with a second container. The user's
// rule: keep this box under ~30 GB (lean to 20), one batch at a time, DEFAULT threads (never
// starve cores to save memory), caches bounded so a run fits, and a hard cap a little above the
// soft one. The numbers are MACHINE-SPECIFIC, so nothing here hardcodes them: every default is a
// fraction of the machine's RAM, and every one has an env override.
//
// DERIVATION (same shape as scripts/lib/membudget.sh, now the engine's own default so no launcher
// can forget it; an explicit env value always wins, and `=0` keeps its documented meaning of
// unbounded/off for each lever):
//   budget_mb  = MTG_MEM_BUDGET_MB if set, else MemTotal / 2            (this box: 47 GB -> ~23.5)
//   reserve    = workers x 250 MB   (consumers the pools don't cover: solvememo, transient search)
//   cache_mb   = max(2 GB, budget_mb - reserve)
//   TT cap     = (cache_mb / workers) / 3 / 64 B      entries PER TABLE (MTG_TT_CAP)
//   FSL pool   = 2/5 of cache_mb, in KB, GLOBAL       (MTG_FSL_POOL)
//   FSL cap    = 2,000,000 entries PER DECISION       (MTG_FSL_CAP)
//   plan cache = workers x 100 MB, in KB, GLOBAL      (MTG_PLAN_CACHE_KB)
//   RSS cap    = MTG_RSS_CAP_GB if set, else 3/4 of MemTotal              (this box: ~35 GB)
// Result-neutral by the pools' own contract: bounding them moves wall clock on cache-hungry tail
// games, never a decision. The smoke/regression digests are the check on every change here.
//
// THE WATCHDOG. A detached thread samples this process's resident set every two seconds; past the
// cap it flushes stdout (a batch streams each finished job's line as it lands, so everything
// already reported survives), prints what it knows -- RSS, the cap, the pools' used/hiwater
// tokens from the registered reporters, and the batch's in-flight jobs from the cap context --
// and _Exit(137)s. That is the same outcome as the kernel's OOM kill (the process dies, finished
// rows survive) with three differences that matter: it happens BEFORE the host is starved, it
// names the games that were running, and it says which cache was full.
#include <cstddef>
#include <functional>
#include <string>

namespace membudget
{
// Physical RAM of the machine in MB (0 = unknown, e.g. an unsupported platform).
long long MemTotalMb();
// Number of worker threads the per-thread splits assume (the affinity CPU count).
int Workers();
// Cache budget in MB: MTG_MEM_BUDGET_MB if set (0 = unbounded), else MemTotal / 2 (0 if unknown).
long long BudgetMb();

// Derived DEFAULTS for the result-neutral caches. Each lever's own reader consults its env var
// first and falls back to these only when the var is UNSET. 0 = the lever's "off/unbounded".
std::size_t DefaultTtCapEntries();
long long   DefaultFslPoolKb();
std::size_t DefaultFslCapEntries();
long long   DefaultPlanCacheKb();

// This process's resident set in bytes (0 = unavailable on this platform).
long long CurrentRssBytes();
// Hard cap in bytes: MTG_RSS_CAP_GB (fractional ok; 0 = off), else 3/4 of MemTotal (0 if unknown).
long long RssCapBytes();

// One line for startup logs: the budget, the four derived bounds and the cap.
std::string Describe();

// Short per-module memory tokens ("fsl=1.2G/9.6G") for the heartbeat line and the cap report.
// Register at static-init time (single-threaded); the report may be read from any thread.
void RegisterMemReporter(std::function<std::string()> f);
std::string MemReport();
// Verbose context printed ONLY when the cap trips (the batch runner installs its in-flight list).
void SetCapContext(std::function<std::string()> f);

// Start the watchdog (idempotent; no thread when the cap is 0 or RSS is unreadable). `tag` names
// the process in its messages ("mtg", "mtg-analyze").
void StartRssWatchdog(const char* tag);
}
