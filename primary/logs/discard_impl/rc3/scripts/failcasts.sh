#!/bin/bash
# For every game in a slower list, run both arms with MTG_EXEC_TAP_TRACE and list REAL casts the executor
# attempted but could not pay (a "<card> tapped:" line with nothing tapped and no float change).
R=/workspaces/MagicDeckTester/logs/discard_impl/rc3
LIST=$1; SUB=$2
while read -r deck cell gis olds news seeds; do
  gi=${gis#gi=}; seed=${seeds#seed=}
  [ -z "$gi" ] && continue
  for a in new old; do
    out=$R/$SUB/et_g${gi}_${cell}_$a
    ENV_EXTRA="MTG_EXEC_TAP_TRACE=1" $R/scripts/run1.sh $deck $cell $gi $seed $a $out
  done
  echo "#### $deck $cell gi=$gi $olds $news"
  for a in new old; do
    out=$R/$SUB/et_g${gi}_${cell}_$a
    python3 - "$out/stderr.txt" "$a" <<'EOF'
import sys, re
path, arm = sys.argv[1], sys.argv[2]
fails = []
for line in open(path, errors='replace'):
    m = re.match(r'\[exec-tap\] T(\d+) (.+?) tapped:\s*float\{w0 u0 b0 r0 g0 c0 \*0\}\s*$', line.rstrip('\n'))
    if m:
        fails.append('T%s %s' % (m.group(1), m.group(2)))
print('  %s failed casts: %s' % (arm, fails if fails else '-'))
EOF
  done
done < "$LIST"
