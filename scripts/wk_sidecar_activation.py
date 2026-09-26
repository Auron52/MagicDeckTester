#!/usr/bin/env python3
"""Does the ADOPTED sidecar actually reproduce the arm that was screened?

The shape screen measured its arms through per-JOB manifest keys (`value_profile: "noleaf"`,
`constant_alpha_relaxed: true`) with `ignore_play_profile: true`. Production does not work that way:
it reads `decks/<Deck>/<Deck>.value.json`, and the file's mere PRESENCE is the activation mechanism.
Those are two different routes to the same intended engine state, and "should be equivalent" is
exactly the kind of assumption this repo has been burned by -- a screen arm that measured one shape
while the deck ships another is the `screen-arm-must-pin-every-key` failure.

So this checks the claim directly, three ways, on the SIDECAR route (no arm keys, no
ignore_play_profile -- the deck exactly as it ships):

  sidecar    the deck as it now ships (value.json present)
  armkeys    the screen's esc_nl arm keys, ignore_play_profile -- what was actually measured
  bare       value.json temporarily moved aside -- the pre-adoption baseline

PASS means: sidecar == armkeys byte-for-byte on every game (the adopted file reproduces the measured
shape), AND sidecar differs from bare somewhere (the file is doing something -- the
`digest-equality-can-mean-BROKEN` check, because a sidecar that silently failed to load would also
match "the baseline" and look fine).

Run with the box otherwise idle: the d5 configs carry a wall-clock budget.
"""
import importlib.util
import json
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DECK = os.path.join(ROOT, "decks/WhiteKnights/WhiteKnights.cod")
PROF = os.path.join(ROOT, "decks/WhiteKnights/WhiteKnights.profile.json")
SIDE = os.path.join(ROOT, "decks/WhiteKnights/WhiteKnights.value.json")
OUT = os.path.join(ROOT, "logs/wk_shape/activation")

spec = importlib.util.spec_from_file_location("shape_probe", os.path.join(ROOT, "scripts/shape_probe.py"))
SP = importlib.util.module_from_spec(spec); spec.loader.exec_module(SP)
ESC_NL = SP.ARMS["esc_nl"]

CONFIGS = [("d3b10", 3, 10), ("d3b20", 3, 20), ("d5b20", 5, 20), ("d5b40", 5, 40)]
SEEDS, GAMES = 4, 250


def read_wins(path):
    out = {}
    if not os.path.exists(path):
        return out
    for line in open(path):
        f = line.split()
        if len(f) >= 3:
            try:
                out[int(f[0])] = (int(f[1]), f[2])
            except ValueError:
                pass
    return out


def run(tag, extra_keys, use_sidecar):
    """One pooled batch for this route. `use_sidecar` False => move value.json aside first."""
    gl = os.path.join(OUT, "gl_" + tag)
    shutil.rmtree(gl, ignore_errors=True)
    os.makedirs(gl, exist_ok=True)
    jobs = []
    for ctag, d, b in CONFIGS:
        for s in range(SEEDS):
            j = {"name": f"{ctag}_s{s}", "deck": DECK, "profile": PROF,
                 "games": GAMES, "seed": 6610000 + s * 1000,
                 "depth": d, "budget_ms": b, "max_turns": 8}
            j.update(extra_keys)
            jobs.append(j)
    mf = os.path.join(OUT, f"manifest_{tag}.json")
    json.dump({"jobs": jobs}, open(mf, "w"), indent=1)

    stash = SIDE + ".ACTIVATION_STASH"
    moved = False
    try:
        if not use_sidecar and os.path.exists(SIDE):
            os.rename(SIDE, stash); moved = True
        with open(os.path.join(OUT, tag + ".out"), "w") as fo, \
             open(os.path.join(OUT, tag + ".err"), "w") as fe:
            subprocess.run([os.path.join(ROOT, "build/Release/mtg"), "--batch", mf,
                            "--game-log-dir", gl], stdout=fo, stderr=fe, check=True, cwd=ROOT)
    finally:
        # ALWAYS put it back, including on a crash -- leaving the deck's shipping sidecar moved
        # aside would silently un-adopt the shape for every later run on this machine.
        if moved and os.path.exists(stash):
            os.rename(stash, SIDE)
    res = {}
    for ctag, _d, _b in CONFIGS:
        for s in range(SEEDS):
            for gi, v in read_wins(os.path.join(gl, f"{ctag}_s{s}.wins")).items():
                res[(ctag, s, gi)] = v
    return res


def main():
    if not os.path.exists(SIDE):
        raise SystemExit(f"no sidecar at {SIDE} -- nothing to verify")
    os.makedirs(OUT, exist_ok=True)

    print("route: sidecar  (the deck as it ships)");        sidecar = run("sidecar", {}, True)
    print("route: armkeys  (what the screen measured)");    armkeys = run("armkeys", {**ESC_NL, "ignore_play_profile": True}, True)
    print("route: bare     (sidecar moved aside)");         bare = run("bare", {}, False)

    print(f"\n== sidecar-activation check -- {len(CONFIGS)} configs x {SEEDS} x {GAMES} games\n")
    print(f"   {'config':<7} {'sidecar==armkeys':>18} {'sidecar==bare':>15}   verdict")
    all_ok = True
    any_live = False
    for ctag, _d, _b in CONFIGS:
        ks = sorted(k for k in sidecar if k[0] == ctag)
        same_arm = sum(1 for k in ks if k in armkeys and sidecar[k] == armkeys[k])
        same_bare = sum(1 for k in ks if k in bare and sidecar[k] == bare[k])
        reproduces = same_arm == len(ks)
        all_ok = all_ok and reproduces
        any_live = any_live or (same_bare < len(ks))
        # LIVENESS IS GLOBAL, NOT PER-CONFIG. An earlier version of this script flagged any config
        # matching `bare` with "the sidecar may not be loading", which is wrong and actively
        # misleading: the divergence RATE differs hugely by config (the 72,000-game screen saw only
        # ~6 differing games per 4000 at d5b40), so a config with a low rate legitimately shows zero
        # on a 1000-game sample. It is the same file and the same load path for every row, so one
        # row differing proves it loads for all of them.
        note = "reproduces the measured arm" if reproduces else "MISMATCH vs the measured arm"
        if reproduces and same_bare == len(ks):
            note += " (no bare divergence at this sample -- expected where the rate is low)"
        print(f"   {ctag:<7} {same_arm:>9}/{len(ks):<8} {same_bare:>7}/{len(ks):<7}   {note}")
    print()
    print("   sidecar==armkeys must be ALL for every config: the adopted file reproduces the shape")
    print("   that was screened.")
    print("   sidecar==bare below all in AT LEAST ONE config is the liveness half -- it proves the")
    print("   file is actually loading, so a match with the measured arm cannot be a silent no-op")
    print("   (the digest-equality-can-mean-BROKEN check). Liveness is global: same file, same path.")
    print(f"\n   reproduces the screened arm everywhere: {'YES' if all_ok else 'NO'}")
    print(f"   sidecar demonstrably loading (>=1 config diverges from bare): {'YES' if any_live else 'NO'}")
    print(f"\n   VERDICT: {'PASS' if (all_ok and any_live) else 'FAIL'}")
    return 0 if (all_ok and any_live) else 1


if __name__ == "__main__":
    sys.exit(main())
