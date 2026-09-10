#!/usr/bin/env python3
"""THE CHEAP EARLY TEST: pick a NEW deck's search shape before deciding whether to generate a value leaf.

User, 2026-09-10: *"For a new deck we may still want to have an early test a bit to decide on an approach to
use prior to value-leaf possible generation."* Value-leaf generation costs hours; the shape decision does not
have to wait for it, because the two shapes a model-less deck can ship are both LEAFLESS:

  heur     no sidecar at all -- the full rollout ladder (the control: what the deck plays today)
  esc_nl   escalation, leaf "none": leafless passes bank proven in-horizon wins, everything else escalates
  fit_nl   ladder "single", leaf "none": the leafless probe runs as deep as its gate allows, then ONE rollout
           pass at the deepest probed depth whose predicted cost fits the remaining budget

All three need NO model, so this is one pooled batch of a few thousand games -- minutes to an hour, against
the many hours a value leaf costs. It answers "which leafless shape" and, from the size of the gap, whether a
leaf is worth generating at all.

  python3 scripts/shape_probe.py decks/<Deck> [--depth 5 --budget-ms 20] [--seeds 4] [--games 500]
  python3 scripts/shape_probe.py decks/<Deck> --dry-run          # print the manifest, run nothing

Reads BOTH axes the adoption rule asks for (docs/design/per-deck-search-shape.md): deterministic search units
and the PAIRED SIGN TEST on per-game win turns (z = net / sqrt(better + worse)) -- not the average win turn,
whose standard error over a whole 8 x 500-game screen is 8-22x the tolerance on the heavy decks. If the
decisive z comes back inside +-2 the run says so and tells you to add seeds rather than pretending it decided.
"""
import argparse, json, math, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# The menu's model-less shapes. `heur` is the control and must stay first. EVERY shape key is written
# EXPLICITLY on every arm: a per-job arm field overrides the deck's own value_play, so a deck that already
# ships a shape cannot leak it into the control (or into the other arm) and quietly compare a shape to itself.
ARMS = {
    "heur":   {"value_model": False, "esc_single": False},
    "esc_nl": {"value_profile": "noleaf", "constant_alpha_relaxed": True, "esc_single": False},
    "fit_nl": {"value_profile": "noleaf", "constant_alpha_relaxed": True,
               "esc_single": True, "esc_single_reserve": 2},
}
SHAPE_FOR = {"esc_nl": {"ladder": "escalation", "leaf": "none", "alpha": "relaxed"},
             "fit_nl": {"ladder": "single", "leaf": "none", "alpha": "relaxed"}}


def resolve_deck(path):
    """-> (decklist, profile or None, stem). Accepts a deck FOLDER or a decklist file."""
    p = path if os.path.isabs(path) else os.path.join(ROOT, path)
    if os.path.isdir(p):
        stem = os.path.basename(p.rstrip("/"))
        for ext in (".cod", ".txt"):
            if os.path.exists(os.path.join(p, stem + ext)):
                deck = os.path.join(p, stem + ext); break
        else:
            raise SystemExit(f"no {stem}.cod / {stem}.txt in {p}")
    else:
        deck, stem = p, os.path.splitext(os.path.basename(p))[0]
        if not os.path.exists(deck): raise SystemExit(f"no such decklist: {deck}")
    prof = os.path.join(os.path.dirname(deck), stem + ".profile.json")
    return deck, (prof if os.path.exists(prof) else None), stem


def load_pairs(path, col):
    d = {}
    if not os.path.exists(path): return d
    for l in open(path):
        a = l.split()
        if len(a) > col:
            try: d[int(a[0])] = int(a[col])
            except ValueError: pass
    return d


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("deck")
    ap.add_argument("--depth", type=int, default=5)
    ap.add_argument("--budget-ms", type=int, default=20)
    ap.add_argument("--seeds", type=int, default=4)
    ap.add_argument("--games", type=int, default=500)
    ap.add_argument("--max-turns", type=int, default=8)
    ap.add_argument("--seed0", type=int, default=880000)
    ap.add_argument("--threads", type=int, default=0)
    ap.add_argument("--out", default=None)
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()

    deck, prof, stem = resolve_deck(a.deck)
    side = os.path.join(os.path.dirname(deck), stem + ".value.json")
    if os.path.exists(side):
        vp = (json.load(open(side)) or {}).get("value_play") or {}
        print(f"NOTE: {stem} already has a value sidecar (value_play {vp or 'absent'}). This probe measures the"
              f"\n      MODEL-LESS shapes only; with a trusted model the escalation-with-leaf shape is the one"
              f"\n      to beat, and that comparison belongs in a full screen, not here.\n")
    out = a.out or os.path.join(ROOT, "logs", "shape_probe", stem)
    os.makedirs(out, exist_ok=True)
    cfg = f"d{a.depth}b{a.budget_ms}"
    seeds = [a.seed0 + 500 * i for i in range(a.seeds)]
    jobs = []
    for arm, extra in ARMS.items():
        for s in seeds:
            j = dict(name=f"{stem}_{arm}_{cfg}_s{s}", deck=deck, games=a.games, seed=s,
                     depth=a.depth, budget_ms=a.budget_ms, max_turns=a.max_turns,
                     ignore_play_profile=True)
            if prof: j["profile"] = prof
            j.update(extra)
            jobs.append(j)
    man = os.path.join(out, "manifest.json")
    json.dump({"jobs": jobs}, open(man, "w"), indent=1)
    print(f"{len(jobs)} jobs ({len(ARMS)} arms x {len(seeds)} seeds x {a.games} games) -> {man}")
    if a.dry_run:
        print(json.dumps(jobs[:len(ARMS)], indent=1)); return

    wins = os.path.join(out, "wins"); os.makedirs(wins, exist_ok=True)
    env = {**os.environ, "MTG_DUMP_UNITS": "1"}
    cmd = [os.path.join(ROOT, "build/Release/mtg"), "--batch", man, "--game-log-dir", wins]
    if a.threads: cmd += ["--threads", str(a.threads)]
    with open(os.path.join(out, "run.out"), "w") as o, open(os.path.join(out, "run.err"), "w") as e:
        p = subprocess.Popen(cmd, stdout=o, stderr=subprocess.PIPE, text=True, cwd=ROOT, env=env)
        for line in p.stderr:
            e.write(line)
            if line.startswith("[batch]") or "SLOW-GAME" in line: print("  | " + line.rstrip(), flush=True)
        rc = p.wait()
    if rc != 0: raise SystemExit(f"batch failed rc={rc}; see {out}/run.err")

    stats = {}
    for arm in ARMS:
        u = 0; w = {}
        for s in seeds:
            tag = f"{stem}_{arm}_{cfg}_s{s}"
            u += sum(load_pairs(os.path.join(wins, tag + ".units"), 1).values())
            for g, t in load_pairs(os.path.join(wins, tag + ".wins"), 1).items(): w[(s, g)] = t
        stats[arm] = (u, w)
    bu, bw = stats["heur"]
    print(f"\n== {stem} {cfg}, {len(seeds)} x {a.games} games, vs the plain rollout ladder (heur)")
    print(f"   {'shape':8} {'units':>12} {'ratio':>7} {'d_avg':>8} {'better':>7} {'worse':>6} {'z':>6}")
    print(f"   {'heur':8} {bu:12} {1.0:7.2f} {0.0:+8.4f} {'-':>7} {'-':>6} {'-':>6}")
    verdict = {}
    for arm in ARMS:
        if arm == "heur": continue
        u, w = stats[arm]
        common = sorted(set(w) & set(bw))
        better = sum(1 for k in common if w[k] < bw[k]); worse = sum(1 for k in common if w[k] > bw[k])
        d = (sum(w[k] for k in common) - sum(bw[k] for k in common)) / max(1, len(common))
        z = (better - worse) / math.sqrt(better + worse) if better + worse else 0.0
        print(f"   {arm:8} {u:12} {u / max(1, bu):7.2f} {d:+8.4f} {better:7} {worse:6} {z:+6.1f}")
        verdict[arm] = (u / max(1, bu), d, z)

    ok = {k: v for k, v in verdict.items() if v[2] >= -2.0 and v[1] <= 0.005}
    print()
    if not ok:
        print("   VERDICT: neither leafless shape holds quality on this deck -- it keeps the rollout ladder.")
    else:
        best = min(ok, key=lambda k: ok[k][0])
        rest = sorted(k for k in ok if k != best)
        print(f"   VERDICT: {best} (units {ok[best][0]:.2f}x the rollout ladder, z {ok[best][2]:+.1f})"
              + (f"; also acceptable: {', '.join(rest)}" if rest else ""))
        print(f"   Put this in decks/{stem}/{stem}.value.json (the file's PRESENCE is what activates it):")
        print("     " + json.dumps({"value_play": SHAPE_FOR[best]}))
    marginal = [k for k, v in verdict.items() if abs(v[2]) < 2.0]
    if marginal:
        print(f"\n   UNRESOLVED at this sample: {', '.join(marginal)} (|z| < 2 vs the ladder). The shapes may"
              f"\n   genuinely tie -- light decks tie exactly -- but if the units differ and you need the"
              f"\n   quality call, re-run with --seeds {a.seeds * 2}.")


if __name__ == "__main__":
    main()
