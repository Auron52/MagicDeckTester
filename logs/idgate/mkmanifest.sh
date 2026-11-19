#!/usr/bin/env bash
# mkmanifest.sh <mode> [deck,deck...]  -> the tier's manifest jobs (regression.sh's own emit_jobs), to stdout
set -u
cd "$(dirname "$0")/../.."
HERE=test
source test/regression_cases.sh
declare -A JOB_COST_U=()
while read -r _jk _ju; do JOB_COST_U[$_jk]=$_ju; done < <(python3 test/lib/job_cost.py emit test/job_cost.tsv)
eval "$(sed -n '/^emit_jobs() {/,/^}/p' test/regression.sh)"
MODE=$1
case "$MODE" in smoke) CASES=( "${SMOKE_CASES[@]}" ) ;; regression) CASES=( "${REGRESSION_CASES[@]}" ) ;; overnight) CASES=( "${OVERNIGHT_CASES[@]}" ) ;; esac
if [ -n "${2:-}" ]; then
  f=(); for spec in "${CASES[@]}"; do set -- $spec; for d in ${2//,/ }; do [ "$1" = "$d" ] && f+=("$spec"); done; done; CASES=( "${f[@]}" )
fi
printf '{ "jobs": [\n%s\n] }\n' "$(emit_jobs)"
