#!/usr/bin/env python3
"""Decision-slice re-gate for the Maze's End APPLY-DROP work (claude_maze_applydrop).

usage: slice.py BIN OUTDIR SLICE_JSON ARMS_JSON [--threads N] [--env K=V ...]
  SLICE_JSON: [[seed, turn, flags_at_gate], ...]
  ARMS_JSON : {"arm_tag": {"MTG_X": true, ...}, ...}   (per-job heurarm flags; {} = env defaults)
One pooled `mtg --batch` PER TURN (MTG_MANA_ORACLE_ONLY_TURN is process-wide), every arm of every game in it.
Counts the ISSUE flag lines per arm and class off the stderr (job names "<arm>__g<gi>").
cwd must be a checkout with decks/. Writes OUTDIR/summary.json and OUTDIR/DONE.
"""
import collections, json, os, re, subprocess, sys

args = sys.argv[1:]
binp, out, slice_p, arms_p = args[:4]
threads = 2
env = dict(os.environ, MTG_MANA_ORACLE="2")
i = 4
while i < len(args):
    if args[i] == "--threads":
        threads = int(args[i + 1]); i += 2
    elif args[i] == "--env":
        k, v = args[i + 1].split("=", 1); env[k] = v; i += 2
    else:
        sys.exit("bad arg " + args[i])
os.makedirs(out, exist_ok=True)
sl = json.load(open(slice_p))
arms = json.load(open(arms_p))
deck = "decks/Maze's End/Maze's End.cod"
prof = "decks/Maze's End/Maze's End.profile.json"
by_turn = collections.defaultdict(list)
for s, t, _ in sl:
    by_turn[t].append(s)
rx = re.compile(r"^\[mana-oracle\] ([A-Z-]+) seed=\d+ job=(\S+)")
counts = collections.Counter()
for t in sorted(by_turn):
    jobs = []
    for s in by_turn[t]:
        gi = s - 30000000
        for tag, fl in arms.items():
            j = {"name": "%s__g%d" % (tag, gi), "deck": deck, "profile": prof, "games": 1, "seed": s, "game_index": gi}
            if fl:
                j["flags"] = fl
            jobs.append(j)
    man = os.path.join(out, "manifest.T%d.json" % t)
    json.dump({"jobs": jobs}, open(man, "w"))
    e = dict(env, MTG_MANA_ORACLE_ONLY_TURN=str(t))
    with open(os.path.join(out, "T%d.out" % t), "w") as so, open(os.path.join(out, "T%d.err" % t), "w") as se:
        p = subprocess.Popen([binp, "--batch", man, "--threads", str(threads)], stdout=so, stderr=subprocess.PIPE,
                             env=e, text=True, errors="replace")
        for ln in p.stderr:
            m = rx.match(ln)
            if m:
                tag = m.group(2).split("__g")[0]
                counts[(tag, m.group(1))] += 1
                if m.group(1) in ("APPLY-DROP", "APPLY-DROP-ORDER", "ENUM-GAP", "ORACLE-GAP"):
                    se.write(ln)
            elif "[batch]" in ln or "SLOW" in ln or "error" in ln.lower():
                se.write(ln)
        p.wait()
    json.dump({"%s|%s" % k: v for k, v in sorted(counts.items())}, open(os.path.join(out, "summary.json"), "w"),
              indent=1)
open(os.path.join(out, "DONE"), "w").write("ok\n")
