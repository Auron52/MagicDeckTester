#!/usr/bin/env python3
"""BATCHED matrix runner: fills the CPU by running many (seed,depth) cells CONCURRENTLY from a job pool,
instead of one cell at a time. This kills the "long tail" waste -- a single mtg call runs N games across its
threads, and when the last few HARD games grind on a couple cores the rest idle; with several cells in flight,
one cell's tail overlaps another cell's bulk, so all cores stay busy.

Incremental durability is PRESERVED: each cell is its own mtg process and emits its per-(seed,depth) block the
instant it finishes (thread-safe, flushed). A cancel keeps every finished cell. Read back with
valueleaf_table_to_metadata.py --average-seeds (per-seed emits averaged per depth).

Memory is bounded by pool*threads_per (total concurrent search threads) AND by pool (concurrent deep cells):
each deep search thread holds a transposition table, so keep pool*threads_per near the core count and pool
small enough that pool*(per-cell RSS) fits RAM. Tasks are submitted in PRIORITY order (the pool picks them up
in order as slots free), so expensive/important cells start first."""
import argparse, os, sys, threading
from concurrent.futures import ThreadPoolExecutor, as_completed
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import valueleaf_depth_matrix as vm


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tasks", nargs="+", required=True,
                    help="priority-ordered 'deck:ARM:depth:games:seed' (ARM in {V,H}), e.g. hinata:V:8:500:8008")
    ap.add_argument("--pool", type=int, default=6, help="max concurrent cells (bounds deep-cell RAM)")
    ap.add_argument("--threads-per", type=int, default=4, help="mtg worker threads per cell (pool*this ~= cores)")
    ap.add_argument("--value-min-depth", type=int, default=0)
    ap.add_argument("--out", help="single output log (ignored if --route)")
    ap.add_argument("--route", action="store_true",
                    help="route each cell to its per-deck log: hinata V->_hinata_vinc, hinata H->_hinata_hinc, "
                         "else <deck>_d68. Lets one pool co-schedule cells across decks/arms while keeping the "
                         "logs the finalize expects.")
    a = ap.parse_args()

    lock = threading.Lock(); handles = {}
    def logpath(deck, arm):
        if not a.route:
            return a.out
        if deck == "hinata":
            return "logs/eval/valueleaf_depth_hinata_%s.txt" % ("vinc" if arm == "V" else "hinc")
        return "logs/eval/valueleaf_depth_%s_d68.txt" % deck
    def emit(s, deck="", arm=""):
        p = logpath(deck, arm)
        with lock:
            print(s, flush=True)
            h = handles.get(p)
            if h is None:
                os.makedirs(os.path.dirname(p), exist_ok=True); h = handles[p] = open(p, "a")
            h.write(s + "\n"); h.flush()

    tasks = []
    for spec in a.tasks:
        deck, arm, d, g, seed = spec.split(":")
        tasks.append((deck, arm.upper(), int(d), int(g), int(seed)))
    emit("\n########## BATCHED pool=%d threads_per=%d tasks=%d -- concurrent cells, per-seed durable ##########"
         % (a.pool, a.threads_per, len(tasks)))

    def run_one(t):
        deck_key, arm, d, g, seed = t
        deck, prof, mt = vm.DECKS[deck_key]
        value_on = (arm == "V")
        try:
            lp, ms = vm.run(deck, d, g, seed, mt, a.threads_per, prof, value_on, a.value_min_depth)
        except Exception as e:
            emit("  %s CELL-ERROR %s%d g=%d seed=%d: %s" % (deck_key, arm, d, g, seed, e), deck_key, arm); return
        # per-(seed,depth) durable 1-seed block (same format as valueleaf_incremental.py)
        blk = ("\n===== DEPTH MATRIX (UNBOUNDED)  games=%d seeds=[%d] value_min_depth=%d  [PURE value-leaf, no redo]  hgames=%d =====\n"
               % (g, seed, a.value_min_depth, g))
        blk += "---- %s (mean over 1 seeds) ----\n" % deck_key
        blk += ("  value-leaf: V%d=%.4f[%.1fms]\n" if value_on else "  heuristic:  H%d=%.4f[%.1fms]\n") % (d, lp, ms)
        blk += "  # DONE %s%d seed %d  (lp=%.4f, %.1f ms/game)" % (arm, d, seed, lp, ms)
        emit(blk, deck_key, arm)

    with ThreadPoolExecutor(max_workers=a.pool) as ex:
        list(as_completed([ex.submit(run_one, t) for t in tasks]))
    for h in handles.values():
        h.close()


if __name__ == "__main__":
    main()
