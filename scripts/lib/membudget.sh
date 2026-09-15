# Memory-budget hook for the long-running generation drivers (sourced by valueleaf.sh and mullgen.sh).
#
# THE DERIVATION NOW LIVES IN THE ENGINE (src/core/MemBudget.h, 2026-09-15). Every result-neutral
# cache -- the transposition table (MTG_TT_CAP), the line cache (MTG_FSL_POOL / MTG_FSL_CAP) and the
# plan caches (MTG_PLAN_CACHE_KB) -- defaults to a bound derived from the machine's RAM inside the
# binary itself, so a launcher that forgets to source this file no longer runs unbounded (that is
# how mullgen.sh ran every mulligan generation with NO caps until 2026-09-06, and how the reference
# bench / regression harness / deck-average scripts ran until 2026-09-15, when one 500 ms EDF game
# reached 30.5 GB and the kernel killed the pooled batch around it).
#
# Engine defaults (each honours its own env var first; `=0` keeps meaning unbounded/off):
#   budget     = MTG_MEM_BUDGET_MB, else MemTotal / 2
#   reserve    = workers x 250 MB;  cache = max(2 GB, budget - reserve)
#   TT cap     = (cache / workers) / 3 / 64 B per table;  FSL pool = 2/5 cache (global KB);
#   FSL cap    = 2,000,000 entries per decision;  plan cache = workers x 100 MB (global)
#   RSS cap    = MTG_RSS_CAP_GB, else 3/4 of MemTotal -- the engine's watchdog aborts the process
#                past it (before the host is starved), printing the in-flight jobs and pool usage.
# The measured history behind those shares is in valueleaf.sh's MEMORY BOUNDS block.
#
# This file therefore exports nothing by default. To give a machine a different budget, export
# MTG_MEM_BUDGET_MB (and/or MTG_RSS_CAP_GB) in the environment before launching -- the user's
# numbers are machine-specific (2026-09-15: ~30 GB soft / ~36 GB hard on the 47 GB shared box).
:
