#!/usr/bin/env bash
# MTG_ETB_WATCHER_GATES A/B on the games that actually cost something.
#
# WHY CPU TIME AND NOT WALL. The gate removes two full battlefield scans per creature entering the
# battlefield. It does not change the search, so the deterministic work meter (`units`) is IDENTICAL
# on both arms by construction -- which is the correctness proof, and useless as a cost signal. The
# cost lives entirely in the per-unit constant, so the measurement has to be a TIME, and wall on
# this box is shared with other tenants and with the run's own siblings.
#
# `instructions` would be the right currency and IS NOT AVAILABLE HERE: this container has no PMU
# access, so `perf stat -e instructions` reports `<not supported>` (only software events work --
# the same limitation the design doc hit when it found perf needs a software event and an output
# file under /tmp). The next best thing is `task-clock:u`, the process's own CPU time: unlike wall
# it does not count the time the process spent descheduled while 23 siblings ran, so it survives
# contention far better. It is still a time and still noisy; the arms are therefore run
# CONCURRENTLY so both see the same box, and the headline is the ratio over the whole set rather
# than any single game.
#
# Runs every (game x arm) concurrently -- independent single-game processes on a 24-core box, so
# this is one tail rather than a serial loop.
#
# usage: etb_gate_ab.sh <out-dir> <seed:gi> [<seed:gi> ...]
set -u -o pipefail

OUT="${1:?usage: etb_gate_ab.sh <out-dir> <seed:gi> [...]}"; shift
mkdir -p "$OUT"
BIN=./build/Release/mtg
DECK=decks/Fungus/Fungus.cod
PROF=decks/Fungus/Fungus.profile.json

pids=()
for spec in "$@"; do
    seed="${spec%%:*}"; gi="${spec##*:}"
    for arm in on off; do
        # The lever is DEFAULT ON, so `on` is the shipped path and `off` restores the pre-gate
        # scan. Both are spelled explicitly -- an arm that relies on a default is an arm nobody can
        # reproduce a year later.
        val=1; [ "$arm" = off ] && val=0
        perf stat -e instructions,task-clock -x, -o "$OUT/g${seed}_${arm}.perf" \
            env MTG_ETB_WATCHER_GATES=$val MTG_SLOW_GAME_MS=1 \
            "$BIN" "$DECK" --profile "$PROF" \
            --seed "$seed" --game-index "$gi" --games 1 --threads 1 \
            > "$OUT/g${seed}_${arm}.log" 2>&1 &
        pids+=($!)
    done
done
echo "launched ${#pids[@]} runs; waiting"
for p in "${pids[@]}"; do wait "$p"; done

python3 - "$OUT" "$@" <<'PY'
import os, re, sys
out = sys.argv[1]
specs = sys.argv[2:]

def perf(path):
    """perf stat -x, writes `value,unit,event,...` per line; blank value means not counted."""
    vals = {}
    if not os.path.exists(path):
        return vals
    for line in open(path):
        parts = line.strip().split(",")
        if len(parts) > 2 and parts[0] not in ("", "<not counted>"):
            try:
                vals[parts[2]] = float(parts[0])
            except ValueError:
                pass
    return vals

def units(path):
    if not os.path.exists(path):
        return None
    m = re.findall(r"units=(\d+)", open(path, errors="replace").read())
    return int(m[-1]) if m else None

def winturn(path):
    if not os.path.exists(path):
        return None
    m = re.findall(r"SLOW-GAME \d+ms\s+gi=\d+ wt=(-?\d+)", open(path, errors="replace").read())
    return int(m[-1]) if m else None

print(f"{'seed':>10} {'wt':>4} {'units on':>13} {'units off':>13} {'cpu_s on':>10} "
      f"{'cpu_s off':>10} {'ratio':>7}")
ti_on = ti_off = 0.0
bad = []
for spec in specs:
    seed = spec.split(":")[0]
    on, off = perf(f"{out}/g{seed}_on.perf"), perf(f"{out}/g{seed}_off.perf")
    uo, uf = units(f"{out}/g{seed}_on.log"), units(f"{out}/g{seed}_off.log")
    wo, wf = winturn(f"{out}/g{seed}_on.log"), winturn(f"{out}/g{seed}_off.log")
    # task-clock is reported in msec by `perf stat -x,`; fall back to instructions if a box
    # ever does expose the PMU.
    io  = on.get("task-clock", 0.0) or on.get("instructions", 0.0)
    if_ = off.get("task-clock", 0.0) or off.get("instructions", 0.0)
    ti_on += io; ti_off += if_
    ratio = (if_ / io) if io else 0.0
    # The two invariants. Either failing means the gate is NOT inert and the cost number is moot.
    if uo != uf: bad.append(f"seed {seed}: units differ {uo} vs {uf}")
    if wo != wf: bad.append(f"seed {seed}: win turn differs {wo} vs {wf}")
    print(f"{seed:>10} {str(wo):>4} {uo if uo is not None else '-':>13} "
          f"{uf if uf is not None else '-':>13} {io/1000.0:>10.1f} {if_/1000.0:>10.1f} "
          f"{ratio:>6.2f}x")
print(f"\nTOTAL cpu seconds: gates ON {ti_on/1000.0:,.1f}   OFF {ti_off/1000.0:,.1f}"
      + (f"   -> {ti_off/ti_on:.3f}x" if ti_on else ""))
if bad:
    print("\n!! INERTNESS VIOLATED -- the gate changed the game, not just its cost:")
    for b in bad: print("   " + b)
else:
    print("\ninertness: units AND win turn identical on every game (the gate skips a scan that "
          "finds nothing)")
PY
