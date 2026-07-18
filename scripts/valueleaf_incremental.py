#!/usr/bin/env python3
"""Incremental, importance-ORDERED value/heuristic matrix runner with PER-SEED durability.

Emits ONE block per (seed, depth) the moment that single mtg run finishes (flushed to disk). This is the
finest unit achievable without changing the C++ (one mtg invocation = one seed x one depth x N games), and
it means a run CANCELLED AT ANY POINT keeps every (seed, depth) that already finished -- nothing in progress
is thrown away. Cells are supplied in PRIORITY order so the important depths land first.

The per-seed 1-seed blocks are exactly the format valueleaf_table_to_metadata.parse_log() consumes; read them
back with `--average-seeds` and the N per-seed emits of a depth are averaged into that depth's mean. Reuses
valueleaf_depth_matrix.run() / .DECKS so cost + parse logic stays in one place. NO timeouts -- it runs until
done or until you kill it, and whatever finished is saved."""
import sys, os, argparse
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import valueleaf_depth_matrix as vm


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--deck", required=True, help="deck key in valueleaf_depth_matrix.DECKS")
    ap.add_argument("--cells", nargs="+", required=True,
                    help="priority-ordered cells 'ARM:depth:games' (ARM in {V,H}), e.g. H:1:300 V:8:500")
    ap.add_argument("--seeds", nargs="+", type=int, default=[8008, 9009])
    ap.add_argument("--value-min-depth", type=int, default=0)
    ap.add_argument("--threads", type=int, default=24)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()

    deck, prof, mt = vm.DECKS[a.deck]
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    of = open(a.out, "a")
    def emit(s): print(s, flush=True); of.write(s + "\n"); of.flush()

    emit("\n########## INCREMENTAL(per-seed) deck=%s cells=%s seeds=%s -- every (seed,depth) saved on finish ##########"
         % (a.deck, ",".join(a.cells), a.seeds))
    for spec in a.cells:
        arm, d, g = spec.split(":"); d = int(d); g = int(g); value_on = (arm.upper() == "V")
        for seed in a.seeds:
            try:
                lp, ms = vm.run(deck, d, g, seed, mt, a.threads, prof, value_on, a.value_min_depth)
            except Exception as e:
                emit("  %s CELL-ERROR %s%d g=%d seed=%d: %s" % (a.deck, arm.upper(), d, g, seed, e)); continue
            # Durable 1-seed block. parse_log(--average-seeds) averages the per-seed emits of a depth.
            emit("\n===== DEPTH MATRIX (UNBOUNDED)  games=%d seeds=[%d] value_min_depth=%d  [PURE value-leaf, no redo]  hgames=%d ====="
                 % (g, seed, a.value_min_depth, g))
            emit("---- %s (mean over 1 seeds) ----" % a.deck)
            if value_on:
                emit("  value-leaf: V%d=%.4f[%.1fms]" % (d, lp, ms))
            else:
                emit("  heuristic:  H%d=%.4f[%.1fms]" % (d, lp, ms))
            emit("  # DONE %s%d seed %d  (lp=%.4f, %.1f ms/game)" % (arm.upper(), d, seed, lp, ms))
    of.close()


if __name__ == "__main__":
    main()
