#!/usr/bin/env python3
"""Overnight FINALIZE: assemble logs in correct provenance order and WRITE the metadata.

  Hinata : value cells (vinc) + heuristic cells (hinc)  -> writer --decks hinata
  5 decks: d6-8 EXTENSIONS first, authoritative pure d1-5 LAST (latest-wins keeps pure for d1-5),
           --scalar-max-depth 5 so deep cells extend the table+crossover but NEVER move the play scalars.

Whatever cells exist in the logs are used (a time-capped heuristic that only reached H1-3 still yields a
crossover -- the writer sorts over present depths). All writes go to git-tracked <deck>.value.json, so a
morning `git diff` shows exactly what changed and `git checkout` reverts. Prints each deck's derived
crossover. Does NOT build, validate, accept GT, or commit."""
import subprocess, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import valueleaf_table_to_metadata as vlt

BASE = "logs/eval"
def cat(paths, out):
    present = [p for p in paths if os.path.exists(p)]
    with open(out, "w") as o:
        for p in present:
            o.write(open(p).read() + "\n")
    return present

def preaverage(in_path, out_path):
    """Collapse a PER-SEED log (valueleaf_incremental.py: one block per (seed,depth)) into a cell-AVERAGED
    log, so it can join the latest-wins 5-deck provenance merge without the last seed silently winning.
    No-op passthrough if the file is missing."""
    if not os.path.exists(in_path):
        return False
    b = vlt.parse_log(in_path, average=True)
    with open(out_path, "w") as o:
        for deck, blk in b.items():
            g = blk["meta"].get("vgames") or blk["meta"].get("games") or 0
            seeds = blk["seeds"]; vmd = blk["meta"].get("value_min_depth", 0)
            hdr = "===== DEPTH MATRIX (UNBOUNDED)  games=%d seeds=%s value_min_depth=%d  [PURE value-leaf, no redo]  hgames=%d =====\n" % (g, seeds, vmd, g)
            o.write(hdr); o.write("---- %s (mean over %d seeds) ----\n" % (deck, len(seeds)))
            if blk["H"]:
                o.write("  heuristic:  " + "   ".join("H%d=%.4f[0.0ms]" % (d, blk["H"][d]) for d in sorted(blk["H"])) + "\n")
            if blk["V"]:
                o.write("  value-leaf: " + "   ".join("V%d=%.4f[0.0ms]" % (d, blk["V"][d]) for d in sorted(blk["V"])) + "\n")
    return True

def writer(log, extra):
    cmd = ["python3", "scripts/valueleaf_table_to_metadata.py", log] + extra
    print("\n>>> " + " ".join(cmd)); subprocess.run(cmd)

def main():
    print("========== FINALIZE: Hinata (per-seed logs -> --average-seeds) ==========")
    used = cat([f"{BASE}/valueleaf_depth_hinata_vinc.txt", f"{BASE}/valueleaf_depth_hinata_hinc.txt"],
               f"{BASE}/_hinata_combined.txt")
    print("logs used:", used)
    writer(f"{BASE}/_hinata_combined.txt", ["--decks", "hinata", "--average-seeds"])

    print("\n========== FINALIZE: 5 decks d6-8 (extensions first, pure LAST, scalar-cap 5) ==========")
    # antilife d6-8 is a PER-SEED log -> pre-average it before the latest-wins merge (else last seed wins).
    preaverage(f"{BASE}/valueleaf_depth_antilife_d68.txt", f"{BASE}/_antilife_d68_avg.txt")
    used = cat([f"{BASE}/valueleaf_depth_burn_d8.txt", f"{BASE}/valueleaf_depth_5deck_d68.txt",
                f"{BASE}/_antilife_d68_avg.txt", f"{BASE}/valueleaf_depth_matrix_pure.txt"],
               f"{BASE}/_5deck_combined.txt")
    print("logs used:", used)
    writer(f"{BASE}/_5deck_combined.txt",
           ["--decks", "antilife", "slivers", "TH", "burn", "knights", "--scalar-max-depth", "5"])
    print("\n========== FINALIZE done. Review with: git diff -- decks/*/*.value.json ==========")


if __name__ == "__main__":
    main()
