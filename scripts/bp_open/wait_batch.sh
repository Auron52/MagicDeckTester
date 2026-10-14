#!/usr/bin/env bash
# Block until a run_batch.sh run has written its exit line. Usage: wait_batch.sh <dir> [suffix]
set -u
D=$1; SUF=${2:-}
while ! grep -q '^exit' "$D/run$SUF.info" 2>/dev/null; do sleep 30; done
tail -1 "$D/run$SUF.info"
