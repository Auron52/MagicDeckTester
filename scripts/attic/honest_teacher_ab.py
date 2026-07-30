#!/usr/bin/env python3
"""Sweep lam per TEACHER (rows file), train --rank, A/B the d0 model over held-out seeds.

For the full-strength honest teacher experiment (learned-d0-policy.md): compares the model a
teacher's labels yield, best-lam vs best-lam, against the heuristic baseline. Metric = aggregate
loss-penalized avg win turn (losses = max_turns+1), lower better. Deterministic runs (no repeats).

    scripts/honest_teacher_ab.py --deck decks/Anti-Lifegain.cod \
        --rows qmodel=logs/eval/antilife_qmodel.rows honest_d2=logs/eval/antilife_honest_d2.rows \
        --seeds 4020 4021 4022 4023 --games 150 --lams 0.03 0.05 0.1 0.15 0.2
"""
import argparse, os, re, subprocess, sys, json, tempfile, shutil

MTG = "build/Release/mtg"


def run_lp(deck, games, seed, max_turns, threads, model):
    env = dict(os.environ)
    for k in ("MTG_EVAL_MODEL", "MTG_EVAL_PROFILE", "MTG_VALUE_MODEL", "MTG_VALUE_PROFILE",
              "MTG_DUMP_EVAL_ROWS", "MTG_DUMP_VALUE_ROWS", "MTG_EVAL_ROWS_HONEST",
              "MTG_EVAL_ROWS_ROLLOUT", "MTG_EVAL_ROLLOUT_DEPTH"):
        env.pop(k, None)
    if model and model.lower() != "none":
        env["MTG_EVAL_MODEL"] = "1"
        env["MTG_EVAL_PROFILE"] = model
    cmd = [MTG, deck, "--games", str(games), "--seed", str(seed),
           "--depth", "0", "--max-turns", str(max_turns), "--threads", str(threads)]
    out = subprocess.run(cmd, capture_output=True, text=True, env=env).stdout
    played = int(re.search(r"Games played\s*:\s*(\d+)", out).group(1))
    won = int(re.search(r"Games won\s*:\s*(\d+)", out).group(1))
    m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out)
    avg = float(m.group(1)) if m else float("nan")
    lost = played - won
    return played, won, (won * avg + lost * (max_turns + 1))


def agg_lp(deck, games, seeds, max_turns, threads, model):
    tp = tw = tsum = 0
    for s in seeds:
        p, w, lp_sum = run_lp(deck, games, s, max_turns, threads, model)
        tp += p; tw += w; tsum += lp_sum
    return tw, tp, tsum / tp  # won, played, aggregate LP


def train(rows, lam, out):
    subprocess.run([sys.executable, "scripts/train_eval_model.py", "--rows", rows,
                    "--rank", "--lam", str(lam), "--out", out],
                   capture_output=True, text=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--deck", required=True)
    ap.add_argument("--rows", nargs="+", required=True, help="name=path pairs")
    ap.add_argument("--seeds", nargs="+", type=int, default=[4020, 4021, 4022, 4023])
    ap.add_argument("--games", type=int, default=150)
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--threads", type=int, default=10)
    ap.add_argument("--lams", nargs="+", type=float, default=[0.03, 0.05, 0.1, 0.15, 0.2])
    args = ap.parse_args()

    print("# deck=%s seeds=%s games=%d (per seed)" % (args.deck, args.seeds, args.games))
    bw, bp, blp = agg_lp(args.deck, args.games, args.seeds, args.max_turns, args.threads, None)
    print("heuristic         won=%d/%d  LP=%.4f" % (bw, bp, blp))

    for spec in args.rows:
        name, path = spec.split("=", 1)
        best = None
        for lam in args.lams:
            fd, tmp = tempfile.mkstemp(suffix=".eval.json", dir="logs/eval"); os.close(fd)
            train(path, lam, tmp)
            if not os.path.exists(tmp):
                print("  %-12s lam=%-5s TRAIN FAILED" % (name, lam)); continue
            w, p, lp = agg_lp(args.deck, args.games, args.seeds, args.max_turns, args.threads, tmp)
            tag = ""
            if best is None or lp < best[0]:
                best = (lp, lam, tmp); tag = "  <-- best"
            print("  %-12s lam=%-5s won=%d/%d  LP=%.4f%s" % (name, lam, w, p, lp, tag))
        if best:
            keep = "logs/eval/%s_%s.eval.json" % (
                os.path.basename(args.deck).split(".")[0].lower(), name)
            os.replace(best[2], keep)
            print("  => %-12s BEST lam=%s LP=%.4f  vs heuristic %+.4f  saved %s" % (
                name, best[1], best[0], best[0] - blp, keep))


if __name__ == "__main__":
    main()
