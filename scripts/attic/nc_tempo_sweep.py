#!/usr/bin/env python3
"""Sweep the NC tempo bonus (MTG_NC_TEMPO) across a deck's reference games on IDENTICAL hands.

For each references/<deck>/*.json, force the ref's exact opening hand and run the NC reshuffle
search at each TEMPO value; tabulate per-game win turns vs the human's saved turn and the
loss-penalised LP per TEMPO. TEMPO=0 is the current (byte-identical) NC policy. Finds whether a
land-drop tempo bonus closes the NC-vs-human play gap without regressing games NC already wins on
curve. See learned-d0-policy.md and ref_bench.py.

    scripts/nc_tempo_sweep.py --deck antilife --tempos 0 0.5 0.7 1.0 --threads 2
"""
import argparse, json, os, re, subprocess, sys, glob
from concurrent.futures import ThreadPoolExecutor

MTG = "build/Release/mtg"
DECKS = {
    "antilife": ("references/Anti-Lifegain", "decks/Anti-Lifegain.cod"),
    "burn":     ("references/burn",           "decks/burn.txt"),
    "slivers":  ("references/slivers_vial",   "decks/slivers_vial.txt"),
    "knights":  ("references/Knights",        "decks/Knights.cod"),
    "TH":       ("references/treasure_hunt",  "decks/treasure_hunt.txt"),
    "hinata":   ("references/Hinata2",        "decks/Hinata2.cod"),
}


def run_nc(deck_file, seed, gi, mull, depth, max_turns, k, ncd, tempo):
    env = dict(os.environ)
    for kk in ("MTG_EVAL_MODEL","MTG_VALUE_MODEL"):
        env.pop(kk, None)
    env["MTG_NC_SEARCH"]="1"; env["MTG_NC_K"]=str(k); env["MTG_NC_DEPTH"]=str(ncd)
    env["MTG_NC_TEMPO"]=str(tempo)
    fm = "%d:%s" % (mull["count"], ",".join(str(x) for x in mull.get("bottom", [])))
    cmd = [MTG, deck_file, "--games","1","--seed",str(seed),"--game-index",str(gi),
           "--depth",str(depth),"--max-turns",str(max_turns),"--force-mulligan",fm,"--threads","1"]
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=200)
    except subprocess.TimeoutExpired:
        return "TMO"
    m = re.search(r"Games won\s*:\s*(\d+)", p.stdout)
    if m is None: return "ERR"
    if int(m.group(1)):
        return int(float(re.search(r"Avg win turn\s*:\s*([\d.]+)", p.stdout).group(1)))
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--deck", required=True, choices=list(DECKS))
    ap.add_argument("--depth", type=int, default=5)
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--nc-k", type=int, default=8)
    ap.add_argument("--nc-depth", type=int, default=2)
    ap.add_argument("--tempos", nargs="+", type=float, default=[0,0.5,0.7,1.0])
    ap.add_argument("--threads", type=int, default=2)
    args = ap.parse_args()
    refdir, deck_file = DECKS[args.deck]
    refs = sorted(glob.glob(os.path.join(refdir, "*.json")))
    mt = args.max_turns; LOSS = mt+1
    tempos = args.tempos

    def work(path):
        r = json.load(open(path))
        seed, gi = r["seed"], r["game_index"]
        mull = r.get("mulligan", {"count":0,"bottom":[]})
        human = r["win_turn"] if r.get("won") else None
        res = {t: run_nc(deck_file, seed, gi, mull, args.depth, mt, args.nc_k, args.nc_depth, t) for t in tempos}
        return (os.path.basename(path), human, res)

    def lp(v): return v if isinstance(v,int) else LOSS
    def cell(v): return str(v) if isinstance(v,int) else (v if v in ("ERR","TMO") else "L")
    hdr = "%-24s %5s | " % ("ref","human") + " ".join("t=%-4g"%t for t in tempos) + " | flags"
    print(hdr); sys.stdout.flush()
    sums = {t:0 for t in tempos}; hsum=0; n=0
    with ThreadPoolExecutor(max_workers=args.threads) as ex:
        for name, human, res in ex.map(work, refs):
            n+=1; hsum+=lp(human)
            for t in tempos: sums[t]+=lp(res[t])
            base = res[tempos[0]]
            flags=[]
            # regression vs baseline tempo (higher win turn = worse)
            for t in tempos[1:]:
                if isinstance(res[t],int) and isinstance(base,int):
                    if res[t] > base: flags.append("t%g:+%d!"%(t,res[t]-base))
                    elif res[t] < base: flags.append("t%g:%d"%(t,res[t]-base))
            row = "%-24s %5s | "%(name, human if human else "L") + " ".join("%-5s"%cell(res[t]) for t in tempos)
            print(row + " | " + " ".join(flags)); sys.stdout.flush()
    print("-"*len(hdr))
    print("%-24s %5.2f | "%("LP AVG", hsum/n) + " ".join("%-5.2f"%(sums[t]/n) for t in tempos) + " | (losses=%d n=%d)"%(LOSS,n))


if __name__ == "__main__":
    main()
