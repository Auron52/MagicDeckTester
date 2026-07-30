#!/usr/bin/env bash
# DETACHED overnight supervisor (run via: setsid nohup bash test/mm6_full_supervisor.sh </dev/null &>logs/mm6_supervisor.out &)
# Runs the full mm6 gen chain to completion, independent of any agent/session:
#   TH R=40  ->  Slivers R=60  ->  Knights R=60   (all max_mull=6, depth=5, chunk+adaptive)
# Each deck's gen is itself resumable (banked rounds are skipped), so a mid-run death only
# loses the in-flight round. Non-destructive (writes logs/<deck>_gen/ only). Commit frozen at HEAD.
set -uo pipefail
cd "$(dirname "$0")/.."
LOG=logs/mm6_supervisor.out
say(){ echo "[$(date -u +%FT%TZ)] $*" | tee -a "$LOG"; }
say "SUPERVISOR start (HEAD=$(git rev-parse --short HEAD), PID=$$)"

say "=== [1/3] TH R=40 (resumes from banked base chunk chunk_s30001000 = the R=1 probe) ==="
KM_DECK=decks/treasure_hunt/treasure_hunt.txt KM_CHUNK_R=1 KM_ROUND_R=5 \
  KM_ROUND_SEED_BASE=30002000 KM_TARGET_R=40 bash test/exhaustive_chunked_gen.sh
say "TH exit=$?"

say "=== [2/3] Slivers R=60 (fresh) ==="
KM_DECK=decks/slivers_vial/slivers_vial.txt KM_TARGET_R=60 KM_ROUND_R=5 \
  bash test/exhaustive_chunked_gen.sh
say "Slivers exit=$?"

say "=== [3/3] Knights R=60 (fresh) ==="
KM_DECK=decks/Knights/Knights.cod KM_TARGET_R=60 KM_ROUND_R=5 \
  bash test/exhaustive_chunked_gen.sh
say "Knights exit=$?"

say "SUPERVISOR COMPLETE. Staged profiles: logs/{treasure_hunt,slivers_vial,Knights}_gen/pooled.profile.json"
