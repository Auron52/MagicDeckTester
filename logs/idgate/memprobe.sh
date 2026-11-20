#!/usr/bin/env bash
# probe.sh <bin> <manifest> <tag> [threads] : one batch under /usr/bin/time -v, RSS sampled every 1 s (VmRSS/RssAnon/RssFile).
set -u
cd /workspaces/MagicDeckTester/.claude/worktrees/idgate
B=$1; M=$2; T=$3; TH=${4:-$(nproc)}; S=/workspaces/MagicDeckTester/build/scratch/idgate/mem
mkdir -p $S/$T
mkdir -p $S/$T; export MTG_RSS_CAP_GB=20 MTG_DUMP_UNITS=1 MTG_TT_STATS=1 MTG_TIER1_STATS=$S/$T/tier1.txt
/usr/bin/time -v -o $S/$T/time.txt "$B" --batch "$M" --threads "$TH" --game-log-dir $S/$T > $S/$T/out 2> $S/$T/err &
tp=$!
sleep 1; mp=$(pgrep -P $tp | head -1)
while kill -0 $tp 2>/dev/null; do
  [ -n "$mp" ] && awk -v t=$(date +%s) '/^VmRSS|^RssAnon|^RssFile/{printf "%s %s ",$1,$2} END{print t}' /proc/$mp/status 2>/dev/null >> $S/$T/rss.txt
  sleep 1
done
wait $tp; echo "rc=$?" > $S/$T/DONE
