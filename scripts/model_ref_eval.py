#!/usr/bin/env python3
"""Fast MODEL-only reference eval: run the land-fold model on each references/<deck> hand (the human's
exact forced opening) and report loss-penalised LP beside the human's recorded win turn. Skips the slow
clairvoyant/NC legs (unchanged across model rounds) so DAgger iterations measure in seconds.

  scripts/model_ref_eval.py --deck antilife --model-dyn logs/model_improve/al_dagger1_dyn.json --d0lf-k 16
"""
import argparse, glob, json, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

MTG = "build/Release/mtg"
DECKS = {
    "antilife": ("references/Anti-Lifegain", "decks/Anti-Lifegain.cod"),
    "burn":     ("references/burn",           "decks/burn.txt"),
    "TH":       ("references/treasure_hunt",  "decks/treasure_hunt.txt"),
}

def run_model(deck_file, seed, gi, mull, mt, env_extra):
    env = {k: v for k, v in os.environ.items() if not k.startswith("MTG_")}
    env.update(env_extra)
    fm = "%d:%s" % (mull["count"], ",".join(str(x) for x in mull.get("bottom", [])))
    cmd = [MTG, deck_file, "--games", "1", "--seed", str(seed), "--game-index", str(gi),
           "--depth", "0", "--max-turns", str(mt), "--force-mulligan", fm, "--threads", "1"]
    # Retry: K16 land-fold games occasionally crash/time out under concurrency (OOM). The run is
    # deterministic, so a retry recovers the real result rather than mislabelling it a loss.
    for attempt in range(3):
        try:
            p = subprocess.run(cmd, capture_output=True, text=True, env=env, timeout=180)
        except subprocess.TimeoutExpired:
            continue
        m = re.search(r"Games won\s*:\s*(\d+)", p.stdout)
        if m is None:
            continue
        if int(m.group(1)):
            return int(float(re.search(r"Avg win turn\s*:\s*([\d.]+)", p.stdout).group(1)))
        return None  # genuine loss
    return "ERR"

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--deck", required=True, choices=list(DECKS))
    ap.add_argument("--model-dyn", default=None)
    ap.add_argument("--model-value", default=None)
    ap.add_argument("--d0lf-k", type=int, default=16)
    ap.add_argument("--card-features", action="store_true", help="serve with MTG_CARD_FEATURES=1")
    ap.add_argument("--max-turns", type=int, default=10)
    ap.add_argument("--threads", type=int, default=12)
    ap.add_argument("--quiet", action="store_true")
    args = ap.parse_args()
    refdir, deck_file = DECKS[args.deck]
    refs = sorted(glob.glob(os.path.join(refdir, "*.json")))
    mt = args.max_turns; LOSS = mt + 1
    env_extra = {"MTG_D0_LANDFOLD": "1", "MTG_D0LF_K": str(args.d0lf_k)}
    if args.card_features: env_extra["MTG_CARD_FEATURES"] = "1"
    if args.model_dyn:   env_extra["MTG_DYN_MODEL"] = args.model_dyn
    if args.model_value: env_extra["MTG_VALUE_PROFILE"] = args.model_value

    def work(path):
        r = json.load(open(path))
        mull = r.get("mulligan", {"count": 0, "bottom": []})
        human = r["win_turn"] if r.get("won") else None
        model = run_model(deck_file, r["seed"], r["game_index"], mull, mt, env_extra)
        return (os.path.basename(path), human, model)

    sh = sm = 0; n = 0; worse = 0
    with ThreadPoolExecutor(max_workers=args.threads) as ex:
        for name, h, m in ex.map(work, refs):
            n += 1
            hl = h if isinstance(h, int) else LOSS
            ml = m if isinstance(m, int) else LOSS
            sh += hl; sm += ml
            if ml > hl: worse += 1
            if not args.quiet:
                flag = ("MODEL>human+%d" % (ml - hl)) if ml > hl else ("model<human" if ml < hl else "")
                print("%-26s human=%s model=%s  %s" % (name, h, m if m is not None else "LOSS", flag))
    print("LP: human=%.3f  MODEL=%.3f   (model worse on %d/%d, losses=%d)" % (sh/n, sm/n, worse, n, LOSS))

if __name__ == "__main__":
    main()
