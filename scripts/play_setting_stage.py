#!/usr/bin/env python3
"""Value-leaf pipeline phase E.7: decide each deck's PLAY SETTING (the live sidecar's enabled `value_play`).

    python3 scripts/play_setting_stage.py decks/<Deck> [decks/<Deck2> ...] [--work DIR] [--decisions FILE] [--write]

Normally run by `bash scripts/valueleaf.sh run` (after E.5 installs the leaf and E.6 its skip allowance) or alone
by `bash scripts/valueleaf.sh playset [decks/<Deck> ...]` for leaves that are already live.

WHY (USER 2026-10-22: "The leaf should have a stage that decides."). A value_play block drives play only when it is
ENABLED, and the documented route to one was a manual recommend -> accept exercise (MulliganProfile.h, ValuePlay).
Nothing in the pipeline performed it any more: 18 of the 30 live sidecars carried no enabled block, so those decks
played the built-in d5 / 20 without anything having decided it, and phase E's target_depth sweep (d-1/d/d+1, all at
budget 20) only printed a table. Vampires 2026-10-22 is the example: d4/d5/d6 all 4.241 at 20 -- the budget binds
there, so depth alone is the wrong axis.

WHAT IT MEASURES. ONE pooled batch over every deck: the live sidecar as it stands (`live`, the baseline: its enabled
block, or the built-in d5 / 20 when it has none) against a grid of ENABLED blocks around the current setting --
target_depth {d-1, d, d+1} (>= 3) x budget_ms {b/2, b, 2b}. Each arm is built on the sidecar route (the live sidecar
plus the block: exactly the file that would be installed) in a variant deck folder whose other files are symlinks to
the real ones, so every arm shares the keep table's cache entry (the cache is keyed by the canonical path). 8
held-out seeds x 1000 paired games per arm, the leaf-adoption A/B's size. The arm whose depth and budget equal
the baseline's IS the baseline and is not run.

WHAT IT DECIDES: the leaf adoption's rule (USER 2026-10-11), through scripts/vlq_adopt_pick.py unchanged. ADOPT the
CHEAPEST arm (work units, never wall-clock) whose quality is NOT NOTABLY WORSE than live (not: delta > 0.002 turns at
paired t > +2); a passing arm that is notably better than that one wins instead. A dearer-but-better arm is a TRADE:
recorded for the USER, never adopted. So the stage can only move a deck to the same or a lower cost.

THE BLOCK. target_depth, budget_ms, enabled = true -- and NOTHING ELSE changes: every other key of an existing block
(the search shape, escalation_cap, emul_r, ...) is kept as it is, so the grid isolates depth x budget. The old phase-E
sweep also set escalation_cap = min(depth, deepest measured H) on every arm; on Vampires (2026-10-22) that cap alone
cost 1.05x units at identical quality (enabled d5/20 + cap vs the default d5/20) and skewed every cell, so the cap is
a SHAPE question for the shape arms, not part of this grid. The MULLIGAN-GENERATION keys
(mull_gen_depth, mull_gen_budget_ms, expected_buckets) are copied in from the profile when the block lacks them: a
sidecar's value_play REPLACES the profile's, and dropping expected_buckets would disarm the keep table's K guard.

--write installs the pick into the live sidecar IN THIS TREE (never committed here: it changes play, so the deck's
smoke + regression GT must be re-accepted with it). Every outcome is appended to --decisions; nothing stops to ask.
Exit 0 when the batch ran (whatever the verdicts), 1 when it failed (nothing is installed then).
"""
import argparse, datetime, json, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import deck_registry                                   # noqa: E402
import esc_skip_stage as esk                           # noqa: E402  (inputs_fp: the resume fingerprint)
from engine_bin import engine_bin, kind                # noqa: E402

DEFAULT_PLAY = (5, 20)          # MulliganProfile::BuiltinDefaultPlay(): what a sidecar with no enabled block plays
# Held out from every other stage's games: offsets 5000.. inside one 100k row block (row dumps use the first 2,500 of
# a deck's block), disjoint from phase E (600000..), the matrix (8008..), E.6 (7.1M / 7.3M).
SEEDS = [7_505_000 + 10_000 * i for i in range(8)]
GAMES = 1000
TOL = 0.002
GEN_KEYS = ("mull_gen_depth", "mull_gen_budget_ms", "expected_buckets")


def now():
    return datetime.datetime.now().strftime('%Y-%m-%dT%H:%M:%S')


def current(vp):
    """(depth, budget, description) the live sidecar plays at."""
    if vp.get("enabled") and vp.get("target_depth"):
        return int(vp["target_depth"]), int(vp.get("budget_ms") or 0), "its enabled block"
    return DEFAULT_PLAY[0], DEFAULT_PLAY[1], "the built-in default (no enabled block)"


def grid(d0, b0):
    depths = sorted({d for d in (d0 - 1, d0, d0 + 1) if d >= 3})
    budgets = sorted({max(1, round(b0 / 2)), b0, 2 * b0}) if b0 > 0 else [0]
    return [(d, b) for d in depths for b in budgets]


def block(live_doc, prof_doc, d, b):
    vp = dict(live_doc.get("value_play") or {})
    pvp = prof_doc.get("value_play") or {}
    for k in GEN_KEYS:
        if k not in vp and k in pvp:
            vp[k] = pvp[k]
    vp["target_depth"], vp["budget_ms"], vp["enabled"] = d, b, True
    return vp


def variant(deck, arm, doc, root):
    """A deck folder differing from the real one ONLY in its value.json (the rest symlinked by real path)."""
    dest = os.path.join(root, deck.key, arm)
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    os.makedirs(dest)
    for f in os.listdir(deck.dir):
        src = os.path.join(deck.dir, f)
        if f == deck.stem + ".value.json" or not os.path.isfile(src):
            continue
        os.symlink(os.path.realpath(src), os.path.join(dest, f))
    with open(os.path.join(dest, deck.stem + ".value.json"), "w") as fo:
        json.dump(doc, fo)
    return os.path.join(dest, os.path.basename(deck.deck_file)), os.path.join(dest, deck.stem + ".profile.json")


def run_batch(mtg, work, label, jobs):
    """ONE pooled batch; a finished one whose manifest AND input contents are unchanged is reused (resume)."""
    man = os.path.join(work, label + '.manifest.json')
    log, err = os.path.join(work, label + '.log'), os.path.join(work, label + '.err')
    fpf = os.path.join(work, label + '.inputs.sha256')
    text = json.dumps({'jobs': jobs}, indent=1)
    fp = esk.inputs_fp(mtg, jobs)
    if (os.path.exists(man) and open(man).read() == text and os.path.exists(fpf)
            and open(fpf).read().strip() == fp and os.path.exists(log)
            and '=== BATCH done' in open(log, errors='replace').read()):
        print(f"[E.7] {label}: reusing the finished batch in {work} (inputs fingerprint {fp[:12]} unchanged)", flush=True)
        return log
    if os.path.exists(fpf):
        os.remove(fpf)
    open(man, 'w').write(text)
    print(f"[E.7] {label}: {len(jobs)} jobs, {sum(j['games'] for j in jobs)} games ({mtg}, {kind(mtg)})", flush=True)
    with open(log, 'w') as lo, open(err, 'w') as er:
        rc = subprocess.run([mtg, '--batch', man, '--threads', '0'], stdout=lo, stderr=er).returncode
    if rc != 0 or '=== BATCH done' not in open(log, errors='replace').read():
        raise RuntimeError(f"{label} batch failed (exit {rc}; 137 = SIGKILL, think OOM) -- see {err}")
    open(fpf, 'w').write(fp + '\n')
    return log


def install(path, vp):
    """Set the sidecar's value_play (position kept when it exists), preserving one-line vs indented layout."""
    raw = open(path).read()
    doc = json.loads(raw)
    doc["value_play"] = vp
    out = json.dumps(doc, indent=1 if raw.count("\n") > 2 else None) + ("\n" if raw.endswith("\n") else "")
    # derive_mullgen_setting finds the block by a TEXT search for "value_play": it must be the only occurrence.
    if out.count('"value_play"') != 1:
        raise RuntimeError(f"{path}: '\"value_play\"' would occur {out.count(chr(34) + 'value_play' + chr(34))} times")
    open(path, "w").write(out)
    if json.load(open(path)).get("value_play") != vp:
        raise RuntimeError(f"{path}: value_play did not round-trip")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('decks', nargs='+', help='deck folders (decks/<Deck> or decks/<Deck>/<variant>)')
    ap.add_argument('--work', help='scratch dir (default logs/play_stage/<keys>)')
    ap.add_argument('--decisions', help='append every verdict here (valueleaf.sh passes <queue>/DECISIONS.txt)')
    ap.add_argument('--write', action='store_true', help='install an adopted setting into the live sidecar')
    a = ap.parse_args()

    reg = {os.path.normpath(d.dir): d for d in deck_registry.discover().values()}
    decks = []
    for dd in a.decks:
        d = reg.get(os.path.normpath(dd))
        if d is None:
            sys.exit(f"no registry entry for {dd} (needs a decklist AND a profile)")
        decks.append(d)
    work = a.work or os.path.join('logs', 'play_stage', '+'.join(sorted(d.key for d in decks)))
    os.makedirs(work, exist_ok=True)
    mtg = engine_bin('mtg')
    notes, report, plan = [], [], {}

    for d in decks:
        if not os.path.exists(d.value):
            notes.append(f"PHASE E.7: {d.key} skipped -- no live value sidecar ({d.value})")
            report.append((d.key, 'skipped: no live leaf'))
            continue
        live_doc, prof_doc = json.load(open(d.value)), json.load(open(d.profile))
        d0, b0, src = current(live_doc.get("value_play") or {})
        arms = {}
        for dd, bb in grid(d0, b0):
            if (dd, bb) == (d0, b0):
                continue                                   # that arm IS the baseline (same depth, budget, shape)
            vp = block(live_doc, prof_doc, dd, bb)
            arms[f"d{dd}b{bb}"] = (vp, variant(d, f"d{dd}b{bb}", dict(live_doc, value_play=vp), os.path.join(work, 'variants')))
        plan[d.key] = dict(deck=d, d0=d0, b0=b0, src=src, arms=arms)

    if plan:
        jobs = []
        for k, p in plan.items():
            d = p['deck']
            for s in SEEDS:
                jobs.append(dict(name=f"{k}-live_s{s}", deck=d.deck_file, profile=d.profile, games=GAMES, seed=s))
                for arm, (_, (vdeck, vprof)) in p['arms'].items():
                    jobs.append(dict(name=f"{k}-{arm}_s{s}", deck=vdeck, profile=vprof, games=GAMES, seed=s))
        # The dearest arms first (bigger budget, then deeper), so the batch ends on a short tail.
        def dearest_first(j):
            m = re.search(r'-d(\d+)b(\d+)_s', j['name'])
            return (-int(m.group(2)), -int(m.group(1))) if m else (0, 0)
        jobs.sort(key=dearest_first)
        log = run_batch(mtg, work, 'grid', jobs)
        lines = open(log, errors='replace').read().splitlines()
        for k, p in plan.items():
            d = p['deck']
            klog = os.path.join(work, f"m_{k}.log")
            with open(klog, 'w') as fo:
                for ln in lines:
                    if ln.startswith(f"{k}-") and ': played=' in ln:
                        fo.write(ln[len(k) + 1:] + "\n")
            table = subprocess.run([sys.executable, os.path.join(HERE, 'vlq_ab_report.py'), klog, 'live'],
                                   capture_output=True, text=True).stdout
            pick = subprocess.run([sys.executable, os.path.join(HERE, 'vlq_adopt_pick.py'), klog, '--tol', str(TOL),
                                   '--candidates', *p['arms']], capture_output=True, text=True)
            if pick.returncode not in (0, 2):
                raise RuntimeError(f"vlq_adopt_pick failed for {k}:\n{pick.stdout}{pick.stderr}")
            verdicts = [l for l in pick.stdout.splitlines() if l.startswith(("VERDICT", "PICK", "  "))]
            m = re.search(r'^PICK arm=(\S+) why=(.*)$', pick.stdout, re.M)
            arm, why = (m.group(1), m.group(2)) if m else ('none', 'no PICK line')
            print(f"=== {k}: play setting -- live ({p['src']}: d{p['d0']} / {p['b0']}) vs the enabled grid ===")
            print(table.rstrip())
            head = (f"live = {p['src']}: d{p['d0']} / budget {p['b0']}; grid {', '.join(p['arms'])}; "
                    f"{len(SEEDS)} seeds x {GAMES} paired games (seeds {SEEDS[0]}..)")
            body = "\n    ".join(verdicts)
            tree = "this tree, NOT committed" if a.write else "NOT applied (run without --write)"
            if pick.returncode == 0 and arm in p['arms']:
                vp = dict(p['arms'][arm][0])
                stamp = (f"E.7 play-setting stage {now()[:10]}: {arm} adopted over {p['src']} (d{p['d0']}/{p['b0']}); "
                         f"{why}")
                vp["note"] = (vp["note"] + " | " + stamp) if vp.get("note") else stamp
                if a.write:
                    install(d.value, vp)
                notes.append(f"PHASE E.7: ADOPTED {k} play setting {arm} (target_depth {vp['target_depth']}, budget_ms "
                             f"{vp['budget_ms']}) -- {tree}\n    {head}\n    {body}\n"
                             f"    next: commit it with the deck's smoke + regression GT re-accepted (it changes play).")
                report.append((k, f"ADOPTED {arm} ({why})"))
            else:
                notes.append(f"PHASE E.7: KEPT {k} at {p['src']} (d{p['d0']} / {p['b0']}) -- {why}\n    {head}\n    {body}")
                report.append((k, f"kept d{p['d0']}/{p['b0']} ({why})"))

    with open(os.path.join(work, 'report.txt'), 'w') as f:
        for t in notes:
            f.write(f"=== {now()}  {t}\n\n")
    if a.decisions:
        with open(a.decisions, 'a') as f:
            for t in notes:
                f.write(f"=== {now()}  {t}\n\n")
    print("[E.7] play setting:")
    for k, t in sorted(report):
        print(f"  {k:28s} {t}")
    print(f"[E.7] details: {os.path.join(work, 'report.txt')}")


if __name__ == '__main__':
    try:
        main()
    except RuntimeError as e:
        print(f"[E.7] FAILED: {e}", file=sys.stderr)
        sys.exit(1)
