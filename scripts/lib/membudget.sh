# Shared memory-budget derivation for every long-running generation driver (source this file).
# Extracted from valueleaf.sh 2026-09-06 after mullgen.sh was found exporting NO caps at all --
# i.e. Sunday's mulligan generation would have run every cache unbounded, the exact configuration
# that OOM'd the 23 GB box six times over 2026-09-05/06.
#
# Every bound here is a RESULT-NEUTRAL memo cap (a refused insert/store just recomputes; play is
# byte-identical -- see TranspositionTable::Cap, the FSL byte pool, and plancache in
# src/ai/TurnSolver.cpp). Capping costs only tail-game wall clock, never a row or a decision.
#
# Derivation (see valueleaf.sh's MEMORY BOUNDS block for the measured history):
#   budget    = MemTotal - 5 GB (system: VSCode server, agent session, containerd, page cache)
#   reserve   = nproc x 250 MB  (per-thread consumers the caches don't cover: the plan-cache
#               budget below, solvememo, and the transient per-game search floor)
#   TT        = 1/3 of the remainder at 64 B/entry, split per worker
#   FSL pool  = 2/5 of the remainder, in REAL KB (byte-accurate accounting)
#   plancache = 100 MB/thread (inside the reserve): byte bound over the two whole-vector<Plan>
#               caches (enummemo promotions + the bp-enum continuation cache)
_mem_mb=$(awk '/MemTotal/{print int($2/1024)}' /proc/meminfo)
_nw=$(nproc)
_budget_mb=$(( _mem_mb - 5120 )); [ "$_budget_mb" -lt 2048 ] && _budget_mb=2048
_reserve_mb=$(( _nw * 250 ))
_cache_mb=$(( _budget_mb - _reserve_mb )); [ "$_cache_mb" -lt 2048 ] && _cache_mb=2048
_pw_kb=$(( _cache_mb * 1024 / _nw ))
export MTG_TT_CAP=$((  _pw_kb * 1024 / 3 / 64   ))
export MTG_FSL_POOL=$(( _cache_mb * 1024 * 2 / 5 ))
export MTG_FSL_CAP=2000000
export MTG_PLAN_CACHE_KB=102400
