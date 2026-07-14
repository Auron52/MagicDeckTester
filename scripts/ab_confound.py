#!/usr/bin/env python3
"""Is the model's CHANGE-AVERSE bias a misplay-of-an-edited-deck confound (fixable) or intrinsic?

The A/B experiment found the FIXED base-trained model over-penalizes big edits: v08_max_removal scored
+0.75 dLP by the model vs +0.15 by the teacher. Hypothesis: the base-trained model just MISPLAYS the
edited deck (unseen states) -> inflated LP, a confound, NOT a real quality read. Test: train a model on
teacher rows FROM THE VARIANT ITSELF (identical recipe) and re-measure. If the variant-trained model's LP
collapses toward the teacher, the bias is misplay -> fixable by training on a variant corpus.

  measures on the v08 deck (eval seeds 11111/22222/33333 x 80):
    - /tmp/antilife_base.dyn   (the experiment's actual model)     ~6.225 expected
    - M_baseR  (base rows, recipe R)  -- recipe control
    - M_v08R   (v08  rows, recipe R)  -- the test
  teacher LP(v08)=4.967 ; base-model LP(base)=5.471 (=> unbiased target ~5.62)
"""
import os, re, subprocess, sys
ROOT = "/workspaces/MagicDeckTester2"
MTG, DT = f"{ROOT}/build/Release/mtg", "/tmp/dyntrain"
BASE_DECK = f"{ROOT}/decks/Anti-Lifegain.cod"
V08_DECK = f"{ROOT}/logs/model_improve/ab/v08_max_removal.cod"
OUT = f"{ROOT}/logs/model_improve/confound"
MT = 8
DUMP_SEEDS = range(40001, 40031)   # 30 seeds, held out from eval
DUMP_GAMES = 8
EVAL_SEEDS = [11111, 22222, 33333]
EVAL_GAMES = 80


def sh(env, args):
    e = {k: v for k, v in os.environ.items() if not k.startswith("MTG_")}
    e.update(env)
    return subprocess.run([MTG] + args, capture_output=True, text=True, env=e).stdout


def dump(deck, rows_out):
    """teacher-labeled rsvalue rows (recipe R: K8 honest d2 rollout), concatenated over DUMP_SEEDS."""
    open(rows_out, "w").close()
    for s in DUMP_SEEDS:
        tmp = f"{OUT}/_chunk.rows"
        sh({"MTG_DUMP_RSVALUE_ROWS": tmp, "MTG_EVAL_ROWS_K": "8", "MTG_EVAL_ROWS_ROLLOUT": "1",
            "MTG_EVAL_ROWS_HONEST": "1", "MTG_EVAL_ROLLOUT_DEPTH": "2"},
           [deck, "--games", str(DUMP_GAMES), "--seed", str(s), "--depth", "0",
            "--max-turns", str(MT), "--threads", "12"])
        if not os.path.exists(tmp):
            continue
        lines = open(tmp).read().splitlines()
        with open(rows_out, "a") as f:
            if os.path.getsize(rows_out) == 0:
                f.write("\n".join(lines) + "\n")
            else:
                f.write("\n".join(lines[1:]) + "\n")
        os.remove(tmp)
    return sum(1 for _ in open(rows_out)) - 1


def train(rows, model_out):
    subprocess.run([DT, rows, "--H", "96", "--epochs", "120", "--out", model_out],
                   capture_output=True, text=True)


def lp_on(deck, model):
    lps = []
    for s in EVAL_SEEDS:
        out = sh({"MTG_D0_LANDFOLD": "1", "MTG_D0LF_K": "16", "MTG_DYN_MODEL": model},
                 [deck, "--games", str(EVAL_GAMES), "--seed", str(s), "--depth", "0",
                  "--max-turns", str(MT), "--threads", "12"])
        p = int(re.search(r"played\s*:\s*(\d+)", out).group(1))
        w = int(re.search(r"won\s*:\s*(\d+)", out).group(1))
        m = re.search(r"Avg win turn\s*:\s*([\d.]+)", out)
        a = float(m.group(1)) if m else 0.0
        lps.append((w * a + (p - w) * (MT + 1)) / p)
    return sum(lps) / len(lps), lps


if __name__ == "__main__":
    os.makedirs(OUT, exist_ok=True)
    print("=== dumping base-deck rows (recipe R) ===", flush=True)
    nb = dump(BASE_DECK, f"{OUT}/base.rows")
    print(f"  base rows={nb}", flush=True)
    print("=== dumping v08 rows (recipe R) ===", flush=True)
    nv = dump(V08_DECK, f"{OUT}/v08.rows")
    print(f"  v08 rows={nv}", flush=True)
    train(f"{OUT}/base.rows", f"{OUT}/M_baseR.dyn")
    train(f"{OUT}/v08.rows", f"{OUT}/M_v08R.dyn")
    print("\n=== LP measured ON THE v08 DECK (lower=faster) ===", flush=True)
    for label, model in [("base.dyn (experiment's model)", "/tmp/antilife_base.dyn"),
                         ("M_baseR (base rows, recipe R)", f"{OUT}/M_baseR.dyn"),
                         ("M_v08R  (v08  rows, recipe R)", f"{OUT}/M_v08R.dyn")]:
        mean, lps = lp_on(V08_DECK, model)
        print(f"  {label:32s} LP={mean:.3f}   ({' '.join('%.2f' % x for x in lps)})", flush=True)
    print("\n  teacher LP(v08)=4.967 ; base-model LP(base-deck)=5.471", flush=True)
    print("  CONFOUND CONFIRMED if M_v08R << M_baseR and -> teacher.", flush=True)
