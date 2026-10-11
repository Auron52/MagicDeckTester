#!/usr/bin/env python3
"""Tiny single-threaded liveness probe: does each USER discard policy FIRE in real searched games?

Per deck: one `mtg --batch` of ONE job (10 games, d3 b10, the policy ON = shipped default) under
MTG_SHED_STATS=1, niced, one thread. Reads the process-wide "SHED STATS" / "SHED POLICY" lines.
A diagnostic, not a measurement (no arms) -- the paired A/B is make_ab.py.
"""
import json, os, re, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_ab

REPO = make_ab.REPO
OUT = os.path.join(REPO, "logs/discard_impl/probe")
os.makedirs(OUT, exist_ok=True)
maps = make_ab.suite_maps()
games = int(sys.argv[1]) if len(sys.argv) > 1 else 10
rows = []
for key in make_ab.DECKS:
    deck, prof = make_ab.EXTRA_FILES.get(key) or (maps["DECK_FILE"][key], maps["DECK_PROF"][key])
    man = os.path.join(OUT, key + ".json")
    json.dump({"jobs": [{"name": key, "deck": deck, "profile": prof, "games": games, "seed": 220001,
                         "depth": 3, "budget_ms": 10, "ignore_play_profile": True}]}, open(man, "w"))
    env = dict(os.environ, MTG_SHED_STATS="1", MTG_PGO="0", MTG_BATCH_HEARTBEAT="0")
    r = subprocess.run(["nice", "-n", "19", "taskset", "-c", "0", os.path.join(REPO, "build/Release/mtg"),
                        "--batch", man, "--threads", "1"], cwd=REPO, env=env, capture_output=True, text=True)
    err = r.stderr
    open(os.path.join(OUT, key + ".err"), "w").write(err)
    st = re.search(r"SHED STATS: real=(\d+)\s+rollout=(\d+)", err)
    po = re.search(r"SHED POLICY: calls=(\d+)\s+idx0-differs-from-generic=(\d+)", err)
    rows.append((key, r.returncode, st.group(1) if st else "-", st.group(2) if st else "-",
                 po.group(1) if po else "-", po.group(2) if po else "-"))
    print("%-16s rc=%d real=%s rollout=%s policy_calls=%s idx0_moved=%s" % rows[-1], flush=True)
