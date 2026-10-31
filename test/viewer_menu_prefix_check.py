#!/usr/bin/env python3
# Usage: python3 test/viewer_menu_prefix_check.py <base mtg> <new mtg> <threads> <reference json>...  (from the repo root)
"""For every main-phase frame of the given references: is the BASE binary's menu an exact prefix of the NEW
binary's menu (plan JSON compared whole, index field included)? Frames come from the base binary's
content-resolved stream (viewer_protocol_check.py --emit-resolved)."""
import json, os, subprocess, sys
from concurrent.futures import ThreadPoolExecutor
if len(sys.argv) < 5:
    sys.exit("usage: viewer_menu_prefix_check.py BASE NEW THREADS REFERENCE [REFERENCE ...]")
BASE, NEW, THREADS = sys.argv[1], sys.argv[2], int(sys.argv[3]); refs = sys.argv[4:]
def resolved(ref):
    env = dict(os.environ, MTG_BIN=BASE)
    out = subprocess.run(["python3", "test/viewer_protocol_check.py", "--emit-resolved", ref], capture_output=True, text=True, env=env).stdout
    return json.loads(out.strip().splitlines()[-1])
def frame(b, r, n):
    env = dict(os.environ, MTG_PLAY_PLANS_CAP="0", MTG_VIEWER_PLAN_CAP_POSITIONS=r['valve'])
    cmd = [b, r['deck'], "--claude-play", "--seed", str(r['seed']), "--game-index", str(r['gi']), "--max-turns", str(r['mt']), "--reveal", "0"]
    if r.get('prof'): cmd += ["--profile", r['prof']]
    if r.get('force'): cmd += ["--force-mulligan", r['force']]
    cmd += r['side'] + ["--choices", ",".join(map(str, r['resolved'][:n]))]
    out = subprocess.run(cmd, capture_output=True, text=True, env=env).stdout
    if "<<<CLAUDE_DECISION>>>" not in out: return None
    return json.loads(out.split("<<<CLAUDE_DECISION>>>")[1].split("<<<END_DECISION>>>")[0])
def check(ref):
    try: r = resolved(ref)
    except Exception as e: return ref, "RESOLVE-ERR " + str(e)[:80], 0, 0
    frames = [f for f in r['frames'] if f.get('recorded_index') is not None]
    bad, n_seen, grew = [], 0, 0
    for f in frames:
        a = frame(BASE, r, f['prefix_len']); c = frame(NEW, r, f['prefix_len'])
        if a is None or a.get('type') != 'main_phase': continue
        n_seen += 1
        if c is None or c.get('type') != 'main_phase':
            bad.append(f"T{a['turn']} {a['phase']} n={f['prefix_len']} missing new main-phase frame")
            continue
        pa, pc = a.get('plans', []), c.get('plans', [])
        if len(pc) > len(pa): grew += 1
        if json.dumps(pa) != json.dumps(pc[:len(pa)]):
            k = next((i for i in range(min(len(pa), len(pc))) if json.dumps(pa[i]) != json.dumps(pc[i])), min(len(pa), len(pc)))
            bad.append(f"T{a['turn']} {a['phase']} n={f['prefix_len']} first diff @{k} ({len(pa)} vs {len(pc)})")
    if not n_seen: return ref, "NO-FRAMES (no main-phase frame checked)", 0, 0
    return ref, ("PREFIX-OK" if not bad else "MISMATCH " + "; ".join(bad[:3])), n_seen, grew
failed = False
with ThreadPoolExecutor(THREADS) as ex:
    for ref, verdict, n, g in ex.map(check, refs):
        print(f"{verdict:10s} {ref}  (main frames {n}, menus grown {g})", flush=True)
        failed = failed or verdict != "PREFIX-OK"
sys.exit(1 if failed else 0)
