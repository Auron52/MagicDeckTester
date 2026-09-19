#!/usr/bin/env bash
# Paired cost A/B for a BYTE-IDENTICAL engine change, measured on the games that actually cost
# something.
#
# WHY TWO BINARIES AND NOT A LEVER. The repo's usual route for a cost A/B is a `heurarm` slot, so one
# pooled `mtg --batch` carries both arms (CLAUDE.md forbids the per-arm wave a process-wide
# `static const bool` would force). That route is for a change whose arms must be COMPARED as
# behaviour. These changes are pure reorderings -- a cheap necessary condition hoisted above an
# expensive one -- so there is no behaviour to select between and a lever would be dead weight in the
# shipped binary forever. The cost is that the two arms are two builds, hence two binaries, hence
# separate processes rather than one batch.
#
# WHAT MAKES THAT SOUND ANYWAY. Every (game x arm) run is launched CONCURRENTLY, so both arms see the
# same box at the same instant -- which is the property the pooled batch was protecting. A serial
# "run A, then run B" on a shared box measures the box's load drift, not the change.
#
# THE CORRECTNESS ASSERTION IS `units`, NOT A TIME. A work unit (ai/GameWorkMeter.h) is a
# deterministic count of turn-steps and interior search nodes, identical for a given (seed,
# game-index) on every machine. A reordering that is byte-identical MUST leave it -- and the win turn
# -- exactly unchanged; if either moves, the change altered the search and the cost numbers are
# meaningless.
#
# THE COST CURRENCY IS CPU TIME, NOT WALL. The first run of this A/B used wall and came back 1.064x
# with one of seven games going the WRONG way -- on a box at loadavg 45 (a 24-worker census plus the
# A/B's own 14 processes), which is a measurement of how the scheduler happened to slice the box, not
# of the change. `task-clock:u` counts only the cycles the process itself was ON a CPU, so it does
# not charge a run for the time it sat descheduled while its 37 neighbours ran. `instructions` would
# be better still and is NOT AVAILABLE HERE: this container has no PMU, so `perf stat -e instructions`
# reports `<not supported>` (software events only). Arms still run concurrently on top of that.
#
# perf's output file must be written OUTSIDE the workspace mount, which breaks it -- hence /tmp.
#
# usage: subset_filter_ab.sh <before-binary> <after-binary> <out-dir> <seed>:<gi> [<seed>:<gi> ...]
set -u -o pipefail

BEFORE="${1:?usage: subset_filter_ab.sh <before-bin> <after-bin> <out-dir> <seed>:<gi> ...}"
AFTER="${2:?need after binary}"
OUT="${3:?need out dir}"
shift 3
mkdir -p "$OUT"
# perf refuses to write its output onto the workspace mount (see the header), so the .perf files live
# in a /tmp scratch dir keyed to this run and only the parsed numbers come back.
PERFDIR="$(mktemp -d /tmp/subset_ab.XXXXXX)"
DECK=decks/Fungus/Fungus.cod
PROF=decks/Fungus/Fungus.profile.json

pids=()
for spec in "$@"; do
    seed="${spec%%:*}"; gi="${spec##*:}"
    for arm in before after; do
        bin="$BEFORE"; [ "$arm" = after ] && bin="$AFTER"
        # MTG_SLOW_GAME_MS=1 forces the per-game SLOW-GAME line (ms + units + win turn) for every
        # game, which is where the byte-identity assertion below is read from.
        perf stat -e task-clock -x, -o "$PERFDIR/g${seed}_${arm}.perf" \
            env MTG_SLOW_GAME_MS=1 "$bin" "$DECK" --profile "$PROF" \
            --seed "$seed" --game-index "$gi" --games 1 --threads 1 \
            > "$OUT/g${seed}_${arm}.log" 2>&1 &
        pids+=($!)
    done
done
echo "launched ${#pids[@]} runs ($# games x 2 arms), all concurrent; waiting"
for p in "${pids[@]}"; do wait "$p"; done

python3 - "$OUT" "$PERFDIR" "$@" <<'PY'
import os, re, sys
out, perfdir, specs = sys.argv[1], sys.argv[2], sys.argv[3:]

def read(path):
    """The SLOW-GAME line carries wall ms, win turn and work units."""
    if not os.path.exists(path):
        return (None, None, None)
    txt = open(path, errors="replace").read()
    m = re.search(r"SLOW-GAME (\d+)ms\s+gi=\d+ wt=(-?\d+) units=(\d+)", txt)
    return (int(m.group(1)), int(m.group(2)), int(m.group(3))) if m else (None, None, None)

def cpu_ms(path):
    """`perf stat -x,` writes `value,unit,event,...`; task-clock's value is in msec."""
    if not os.path.exists(path):
        return None
    for line in open(path):
        parts = line.strip().split(",")
        if len(parts) > 2 and parts[2].startswith("task-clock") and parts[0] not in ("", "<not counted>"):
            try:    return float(parts[0])
            except ValueError: return None
    return None

print(f"{'seed':>9} {'wt':>4} {'units':>12} {'cpu_s before':>13} {'cpu_s after':>12} "
      f"{'speedup':>8} {'wall x':>7}")
tb = ta = 0.0
wb_tot = wa_tot = 0.0
bad = []
for spec in specs:
    seed = spec.split(":")[0]
    mb, wtb, ub = read(f"{out}/g{seed}_before.log")
    ma, wta, ua = read(f"{out}/g{seed}_after.log")
    cb, ca = cpu_ms(f"{perfdir}/g{seed}_before.perf"), cpu_ms(f"{perfdir}/g{seed}_after.perf")
    if None in (mb, ma) or None in (cb, ca):
        bad.append(f"seed {seed}: a run produced no SLOW-GAME line or no task-clock"); continue
    # The two invariants. Either failing means the change was NOT byte-identical, and the cost
    # columns are comparing two different searches rather than two costs for one search.
    if ub != ua: bad.append(f"seed {seed}: units differ {ub} vs {ua}")
    if wtb != wta: bad.append(f"seed {seed}: win turn differs {wtb} vs {wta}")
    tb += cb; ta += ca; wb_tot += mb; wa_tot += ma
    print(f"{seed:>9} {str(wtb):>4} {ub if ub is not None else '-':>12} "
          f"{cb/1000.0:>13.1f} {ca/1000.0:>12.1f} {cb/ca if ca else 0:>7.3f}x "
          f"{mb/ma if ma else 0:>6.3f}x")
print(f"\nTOTAL cpu: before {tb/1000.0:,.1f}s   after {ta/1000.0:,.1f}s"
      + (f"   -> {tb/ta:.3f}x" if ta else ""))
print(f"TOTAL wall (contention-confounded, for reference only): "
      f"{wb_tot/1000.0:,.1f}s -> {wa_tot/1000.0:,.1f}s"
      + (f"   {wb_tot/wa_tot:.3f}x" if wa_tot else ""))
if bad:
    print("\n!! NOT BYTE-IDENTICAL -- the cost numbers above are void:")
    for b in bad: print("   " + b)
else:
    print("\nbyte-identity: units AND win turn identical on every game "
          "(the change reorders tests, it does not change the search)")
PY
