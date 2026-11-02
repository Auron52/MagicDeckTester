#!/usr/bin/env python3
"""Value-leaf pipeline phase E.7: decide each deck's PLAY SETTING (the live sidecar's enabled `value_play`).

    python3 scripts/play_setting_stage.py decks/<Deck> [decks/<Deck2> ...] [--work DIR] [--decisions FILE] [--write]
                                          [--phase-e KEY=<queue>/m_<key>_shape.log ...] [--live KEY=FILE ...]

Normally run by `bash scripts/valueleaf.sh run` (after E.5 installs the leaf and E.6 its skip allowance; it passes
--phase-e) or alone by `bash scripts/valueleaf.sh playset [decks/<Deck> ...]` for leaves that are already live.

WHY (USER 2026-10-22: "The leaf should have a stage that decides."). A value_play block drives play only when it is
ENABLED, and the documented route to one was a manual recommend -> accept exercise (MulliganProfile.h, ValuePlay).
Nothing in the pipeline performed it any more: 18 of the 30 live sidecars carried no enabled block, so those decks
played the built-in d5 / 20 without anything having decided it, and phase E's target_depth sweep (d-1/d/d+1, all at
budget 20) only printed a table. Vampires 2026-10-22 is the example: d4/d5/d6 all 4.241 at 20 -- the budget binds
there, so depth alone is the wrong axis.

WHAT IT MEASURES. ONE pooled batch over every deck: the live sidecar as it stands (`live`, the baseline: its enabled
block, or the built-in d5 / 20 when it has none) against a grid of ENABLED blocks around the current setting --
target_depth {d-1, d, d+1} (>= 5) x budget_ms {b, 1.5b, 2b}. Each arm is built on the sidecar route (the live sidecar
plus the block: exactly the file that would be installed) in a variant deck folder whose other files are symlinks to
the real ones, so every arm shares the keep table's cache entry (the cache is keyed by the canonical path). 8
held-out seeds x 1000 paired games per arm, the leaf-adoption A/B's size. The arm whose depth and budget equal
the baseline's IS the baseline and is not run -- but only when live's block is ENABLED: with no enabled block, the
enabled block at d5 / 20 can still differ from live (an enabled block arms the static W20 beam on every depth-3
search, TurnSolver.cpp's depth-adaptive beam regime; the d5 beam stays off unless the block sets beam_width), so it
is an arm like any other. (Vampires 2026-10-22: byte-identical to live.) `live` itself is a variant folder too, built from the sidecar as read at
the start, so the measurement cannot change if the real file does mid-run.

THE OTHER PHASE-E SHAPES (--phase-e; USER 2026-10-22: "My biggest concern is actually that it didn't happen
automatically for this deck"). Phase E judges every shape (staged leaf, nlesc, nlfit, emul) at ONE budget, the
deck's play budget, and E.5 installs one of them; but a budget is part of a shape -- they do not spend one alike
(docs/design/leaf-vs-heuristic-asymmetries-2026-10-20.md s1, s8: KittyEquipment's emulated ladder won at b14,
Melira Pod's at b60, in the 2026-10-21 fleet screen's equal-cost curves). So every shape phase E measured that is
still in the running gets the SAME grid, its centre included: each one E.5's rule passed but did not install, and
each dearer one that was NOTABLY BETTER than live (a quality edge a lower budget can turn into a saving). They are
re-derived from the queue's shape log by vlq_adopt_pick.py, the same rule on the same games. An adopted shape arm
replaces the whole live sidecar (its model and shape keys too); the previous one is kept in --work.

--live KEY=FILE measures as if FILE were the deck's live sidecar (a what-if, e.g. replaying the stage from the
state an earlier E.5 left; refused with --write).

NEVER BELOW THE CURRENT BUDGET (USER 2026-10-22: "Why are we even considering b10 for play settings?"). The first
grid had b/2, and with the cost-first rule below it moved Vampires to b10 (0.91x units, quality flat). But that rule
was set for the LEAF, a cheaper evaluator under the same search; a lower budget is simply less search -- on a deck
without trust the budget is what pays for the escalation that corrects the leaf -- and the tolerance would let it
give back up to 0.002 turns for any saving. The suite never plays it either (its d5 cases pass their own budget,
which overrides the block's). Higher budgets stay: "Sometimes the higher budgets will actually turn out
particularly well, but that is primarily where you have trust" -- a budget that COMPLETES the trusted depth ends a
decision on the leaf, so it can cost LESS (the trust-depth rule, slivers d5 b10 vs b3); otherwise a dearer arm is
reported as a TRADE.

NEVER BELOW DEPTH 5 (USER 2026-10-29: "It should be depth 5 minimum for play settings."). The grid used to reach
d3; with the cost-first rule it moved AccomplishedAlchemist (2026-10-22) and Giants (2026-10-25) to d4/20 at 0.99x
and 0.94x units, and picked d4/60 for Melira Pod (2026-10-29, +0.0011 t at 0.97x, not landed). Depths below the
floor are clamped to it, so a live setting below d5 is measured only against d5+ arms.

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
MIN_DEPTH = 5                   # USER 2026-10-29: "It should be depth 5 minimum for play settings."
GEN_KEYS = ("mull_gen_depth", "mull_gen_budget_ms", "expected_buckets")
PICK = os.path.join(HERE, 'vlq_adopt_pick.py')


def now():
    return datetime.datetime.now().strftime('%Y-%m-%dT%H:%M:%S')


def current(vp):
    """(depth, budget, description) the live sidecar plays at."""
    if vp.get("enabled") and vp.get("target_depth"):
        return int(vp["target_depth"]), int(vp.get("budget_ms") or 0), "its enabled block"
    return DEFAULT_PLAY[0], DEFAULT_PLAY[1], "the built-in default (no enabled block)"


def grid(d0, b0):
    depths = sorted({max(d, MIN_DEPTH) for d in (d0 - 1, d0, d0 + 1)})     # never below d5 (module doc)
    budgets = sorted({b0, round(1.5 * b0), 2 * b0}) if b0 > 0 else [0]      # never below b0 (module doc)
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


def phase_e_shapes(deck, shape_log):
    """[(arm, sidecar path, reason)]: the phase-E shapes still in the running (module doc), from the E.5 verdicts."""
    r = subprocess.run([sys.executable, PICK, shape_log, '--tol', str(TOL)], capture_output=True, text=True)
    if r.returncode not in (0, 2):
        raise RuntimeError(f"vlq_adopt_pick failed on {shape_log}:\n{r.stdout}{r.stderr}")
    m = re.search(r'^PICK arm=(\S+)', r.stdout, re.M)
    picked = m.group(1) if m else 'none'
    qdir, out = os.path.dirname(shape_log), []
    for ln in r.stdout.splitlines():
        if not ln.startswith('VERDICT'):
            continue
        f = dict(re.findall(r"(\w+)=([^ ]+)", ln.split(" why=")[0]))
        arm, delta, t = f['arm'], float(f['delta']), float(f['t'])
        if arm == picked:
            continue                                       # installed by E.5: it IS live
        if f.get('adopt') == '1':
            why = f"passed E.5's rule at the play budget ({f.get('cost')} vs live, delta {delta:+.5f}) but was not installed"
        elif delta < -TOL and t < -2.0:
            why = f"dearer at the play budget ({f.get('cost')}) but notably better (delta {delta:+.5f}, t {t:+.2f})"
        else:
            continue
        path = (os.path.join('logs', 'eval', f"{deck.stem}.value.STAGED.json") if arm == 'staged'
                else os.path.join(qdir, f"{deck.stem}.value.{arm.upper()}.json"))
        if not os.path.exists(path):
            raise RuntimeError(f"{deck.key}: phase-E shape {arm} {why}, but {path} is missing")
        out.append((arm, path, why))
    return out


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


def install(path, doc):
    """Write the adopted sidecar (value_play position kept), preserving the live file's one-line vs indented layout."""
    raw = open(path).read()
    vp = doc["value_play"]
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
    ap.add_argument('--phase-e', action='append', default=[], metavar='KEY=SHAPELOG',
                    help="the deck's phase-E shape log (<queue>/m_<key>_shape.log): grid its other shapes too")
    ap.add_argument('--live', action='append', default=[], metavar='KEY=FILE',
                    help='measure as if FILE were the live sidecar (a what-if; refused with --write)')
    a = ap.parse_args()
    shape_logs = dict(x.split('=', 1) for x in a.phase_e)
    live_over = dict(x.split('=', 1) for x in a.live)
    if live_over and a.write:
        sys.exit("--live is a what-if: it cannot be combined with --write")

    reg = {os.path.normpath(d.dir): d for d in deck_registry.discover().values()}
    decks = []
    for dd in a.decks:
        d = reg.get(os.path.normpath(dd))
        if d is None:
            sys.exit(f"no registry entry for {dd} (needs a decklist AND a profile)")
        decks.append(d)
    for k in list(shape_logs) + list(live_over):
        if k not in {d.key for d in decks}:
            sys.exit(f"--phase-e / --live names {k}, which is not one of the decks given")
    work = a.work or os.path.join('logs', 'play_stage', '+'.join(sorted(d.key for d in decks)))
    os.makedirs(work, exist_ok=True)
    vroot = os.path.join(work, 'variants')
    mtg = engine_bin('mtg')
    notes, report, plan = [], [], {}

    for d in decks:
        live_path = live_over.get(d.key, d.value)
        if not os.path.exists(live_path):
            notes.append(f"PHASE E.7: {d.key} skipped -- no live value sidecar ({live_path})")
            report.append((d.key, 'skipped: no live leaf'))
            continue
        live_doc, prof_doc = json.load(open(live_path)), json.load(open(d.profile))
        live_vp = live_doc.get("value_play") or {}
        d0, b0, src = current(live_vp)
        centre_is_live = bool(live_vp.get("enabled") and live_vp.get("target_depth"))
        arms = {}
        for dd, bb in grid(d0, b0):
            if (dd, bb) == (d0, b0) and centre_is_live:
                continue                                   # that arm IS the baseline (same block, same shape)
            vp = block(live_doc, prof_doc, dd, bb)
            doc = dict(live_doc, value_play=vp)
            arms[f"d{dd}b{bb}"] = dict(doc=doc, shape=None, paths=variant(d, f"d{dd}b{bb}", doc, vroot))
        shapes = phase_e_shapes(d, shape_logs[d.key]) if d.key in shape_logs else []
        for name, path, why in shapes:
            sdoc = json.load(open(path))
            for dd, bb in grid(d0, b0):
                arm = f"{name}_d{dd}b{bb}"
                vp = block(sdoc, prof_doc, dd, bb)
                doc = dict(sdoc, value_play=vp)
                arms[arm] = dict(doc=doc, shape=(name, path, why), paths=variant(d, arm, doc, vroot))
        plan[d.key] = dict(deck=d, d0=d0, b0=b0, src=src, arms=arms, shapes=shapes,
                           live=variant(d, 'live', live_doc, vroot), live_path=live_path)

    if plan:
        jobs = []
        for k, p in plan.items():
            for s in SEEDS:
                jobs.append(dict(name=f"{k}-live_s{s}", deck=p['live'][0], profile=p['live'][1], games=GAMES, seed=s))
                for arm, x in p['arms'].items():
                    jobs.append(dict(name=f"{k}-{arm}_s{s}", deck=x['paths'][0], profile=x['paths'][1], games=GAMES, seed=s))
        # The dearest arms first (bigger budget, then deeper), so the batch ends on a short tail.
        def dearest_first(j):
            m = re.search(r'[-_]d(\d+)b(\d+)_s', j['name'])
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
            pick = subprocess.run([sys.executable, PICK, klog, '--tol', str(TOL),
                                   '--candidates', *p['arms']], capture_output=True, text=True)
            if pick.returncode not in (0, 2):
                raise RuntimeError(f"vlq_adopt_pick failed for {k}:\n{pick.stdout}{pick.stderr}")
            verdicts = [l for l in pick.stdout.splitlines() if l.startswith(("VERDICT", "PICK", "  "))]
            m = re.search(r'^PICK arm=(\S+) why=(.*)$', pick.stdout, re.M)
            arm, why = (m.group(1), m.group(2)) if m else ('none', 'no PICK line')
            print(f"=== {k}: play setting -- live ({p['src']}: d{p['d0']} / {p['b0']}) vs the enabled grid ===")
            print(table.rstrip())
            sh = "".join(f"\n    shape {n} ({pth}) gridded too: {w}" for n, pth, w in p['shapes'])
            what_if = f" [WHAT-IF: live read from {p['live_path']}]" if p['live_path'] != d.value else ""
            head = (f"live = {p['src']}: d{p['d0']} / budget {p['b0']}{what_if}; grid {', '.join(p['arms'])}; "
                    f"{len(SEEDS)} seeds x {GAMES} paired games (seeds {SEEDS[0]}..){sh}")
            body = "\n    ".join(verdicts)
            tree = "this tree, NOT committed" if a.write else "NOT applied (run without --write)"
            if pick.returncode == 0 and arm in p['arms']:
                x = p['arms'][arm]
                doc = dict(x['doc'])
                vp = dict(doc['value_play'])
                shape_txt = (f"; the phase-E shape {x['shape'][0]} ({x['shape'][1]}) REPLACES the live sidecar"
                             if x['shape'] else "")
                stamp = (f"E.7 play-setting stage {now()[:10]}: {arm} adopted over {p['src']} (d{p['d0']}/{p['b0']})"
                         f"{shape_txt}; {why}")
                vp["note"] = (vp["note"] + " | " + stamp) if vp.get("note") else stamp
                doc['value_play'] = vp
                prev = ""
                if a.write:
                    if x['shape']:
                        prev = os.path.join(work, f"{d.stem}.value.PREV.json")
                        shutil.copyfile(d.value, prev)
                        prev = f"\n    previous live sidecar kept as {prev}"
                    install(d.value, doc)
                notes.append(f"PHASE E.7: ADOPTED {k} play setting {arm} (target_depth {vp['target_depth']}, budget_ms "
                             f"{vp['budget_ms']}{shape_txt}) -- {tree}\n    {head}\n    {body}{prev}\n"
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
