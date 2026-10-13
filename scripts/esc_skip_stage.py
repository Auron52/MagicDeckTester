#!/usr/bin/env python3
"""Value-leaf pipeline phase E.6: decide each deck's escalation-skip allowance (sidecar `escalation_skip_r`).

    python3 scripts/esc_skip_stage.py decks/<Deck> [decks/<Deck2> ...] [--work DIR] [--decisions FILE] [--write]

Normally run by `bash scripts/valueleaf.sh run` (after E.5 installs the leaf, before F) or on its own by
`bash scripts/valueleaf.sh skipcal [decks/<Deck> ...]`. Design and evidence:
docs/design/escalation-skip-calibration-stage.md. The fit and the headroom arithmetic live in
scripts/esc_skip_calibrate.py.

WHAT IT DECIDES. The up-front escalation skip (TurnSolver.cpp, MTG_ESC_SKIP_PREDICT) runs the fleet at R = 0,
which is measured lossless. A deck may carry a per-depth rollout allowance that skips more of the escalations its
take rule throws away -- but a too-large allowance skips one that would have been taken. This stage fits the
deck's own allowance and adopts it only on a held-out A/B with no slower game.

THREE POOLED BATCHES, every deck in each (the steps depend on each other's data, nothing else is a barrier):
  1. PROBE   1200 games per deck at R = 0, play settings, MTG_ESC_SKIP_LOG on.
             GIVE UP (fast) when the deck has no ladder escalations (single-path, or trust-dominated), or when
             the HEADROOM -- every ladder escalation that ran and was thrown away, as a share of all the
             games' work -- is under 4% (USER 2026-10-13: "relatively fast for most decks ... give up if there
             is not a notable amount of headroom. So maybe 4%+"). The gate is that CEILING, not the fit's
             projected saving, on purpose: a deck that clears 4% may project only 2-3% (KittyV3: 5.9% ceiling,
             -3% projected), and the USER (same day) ruled that worth the A/B: "If we get 2-3% that's not a
             waste of time." Otherwise fit the q10 per-depth vector and project what it would save (the
             projection matched the measured A/B within 0.2 points on six decks); a deck projecting 0 gives up.
  2. A/B     the surviving decks, R = 0 vs the vector, paired, on FRESH seeds (the fit never sees them),
             sized to ~15 min of the box per deck (2400..6000 games).
  3. ISOLATE every game whose digest moved, replayed ALONE at both arms as one-game batch chunks. A game that
             is identical alone is batch thread-history flicker (docs/design/batch-run-to-run-nondeterminism.md);
             one that differs alone is a real play change, scored by win turn with no win = max_turns + 1.
VERDICT: an arm PASSES with NO real slower game, net change <= 0 (USER: "+1 is not neutral. 0 is neutral."), and
a unit saving. A deck that already carries an allowance gets a THIRD A/B arm with it (it may be a user-approved
constant that saves more than the fit -- KittyV3's 43.3 -4.2% vs its vector's -3.0% -- or one fitted to an
earlier leaf), and the bar picks between them:
  new passes, existing does not (or none)  -> ADOPT the new vector
  both pass                                 -> the one that saves more units (a tie keeps the existing)
  only the existing passes                  -> KEEP it
  neither passes                            -> REMOVE the existing key (measured lossy on the live leaf) -> R = 0
--write applies it to the live sidecar IN THIS TREE (never committed here; the deck's smoke + regression GT must
be re-accepted with it). A deck that gives up at the probe is not A/B'd, so an existing key is KEPT unmeasured
and DECISIONS.txt flags it. Every outcome is appended to --decisions; nothing stops to ask.

Exit 0 when every batch ran (whatever the verdicts), 1 when a batch failed (nothing is installed then).
"""
import argparse, collections, datetime, json, os, re, subprocess, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import deck_registry                                   # noqa: E402
import esc_skip_calibrate as cal                       # noqa: E402
from engine_bin import engine_bin, kind                # noqa: E402

HEADROOM_MIN = 0.04            # USER 2026-10-13; gates the CEILING, not the projection (USER: 2-3% is worth it)
PROBE_GAMES, PROBE_SEED = 1200, 7_100_000
AB_SEED = 7_300_000            # disjoint from the probe's seeds: the A/B is held out from the fit
AB_WALL_S, AB_MIN, AB_MAX = 900, 2400, 6000
ISO_MAX = 100                  # more moved games than this is not flicker; do not adopt


def now():
    return datetime.datetime.now().strftime('%Y-%m-%dT%H:%M:%S')


def run_batch(mtg, work, label, jobs):
    """Run ONE pooled batch; reuse a finished one whose manifest is unchanged (resume)."""
    man = os.path.join(work, label + '.manifest.json')
    log, err = os.path.join(work, label + '.log'), os.path.join(work, label + '.err')
    text = json.dumps({'jobs': jobs}, indent=1)
    if (os.path.exists(man) and open(man).read() == text and os.path.exists(err)
            and os.path.exists(log) and '=== BATCH done' in open(log, errors='replace').read()):
        print(f"[E.6] {label}: reusing the finished batch in {work}", flush=True)
        return log, err
    open(man, 'w').write(text)
    print(f"[E.6] {label}: {len(jobs)} jobs, {sum(j['games'] for j in jobs)} games ({mtg}, {kind(mtg)})", flush=True)
    env = dict(os.environ, MTG_ESC_SKIP_LOG='1')
    with open(log, 'w') as lo, open(err, 'w') as er:
        rc = subprocess.run([mtg, '--batch', man, '--threads', '0'], stdout=lo, stderr=er, env=env).returncode
    if rc != 0 or '=== BATCH done' not in open(log, errors='replace').read():
        raise RuntimeError(f"{label} batch failed (exit {rc}; 137 = SIGKILL, think OOM) -- see {err}")
    return log, err


def job_ms(log):
    """{job: ms per game} from the batch's per-job summary lines."""
    out = {}
    for ln in open(log, errors='replace'):
        m = re.match(r'^(\S+): played=(\d+) .* ms=(\d+)', ln)
        if m and int(m.group(2)) > 0:
            out[m.group(1)] = int(m.group(3)) / int(m.group(2))
    return out


def install(path, vec):
    """Write escalation_skip_r into a sidecar, touching only that member (format preserved where possible)."""
    raw = open(path, 'rb').read().decode()
    nl = '\r\n' if '\r\n' in raw else '\n'
    val = '[' + ', '.join(f'{x:.1f}' for x in vec) + ']'
    lines = raw.split(nl)
    out = None
    if lines and lines[0].strip() == '{' and len(lines) > 1:
        ind = re.match(r'\s*', lines[1]).group(0)
        for i, ln in enumerate(lines):
            m = re.match(r'^' + re.escape(ind) + r'"escalation_skip_r":\s*(.*)$', ln)
            if not m:
                continue
            j = i
            if m.group(1).startswith('[') and ']' not in m.group(1):      # an array dumped one entry per line
                while j + 1 < len(lines) and ']' not in lines[j]:
                    j += 1
            comma = ',' if lines[j].rstrip().endswith(',') else ''
            out = nl.join(lines[:i] + [f'{ind}"escalation_skip_r": {val}{comma}'] + lines[j + 1:])
            break
        if out is None:
            out = nl.join([lines[0], f'{ind}"escalation_skip_r": {val},'] + lines[1:])
    try:
        ok = out is not None and json.loads(out).get('escalation_skip_r') == vec
    except ValueError:
        ok = False
    if not ok:                                        # unusual layout: rewrite the whole document
        doc = json.loads(raw)
        doc = dict([('escalation_skip_r', vec)] + [(k, v) for k, v in doc.items() if k != 'escalation_skip_r'])
        out = json.dumps(doc, indent=1) + nl
    open(path, 'wb').write(out.encode())


def remove(path):
    """Delete escalation_skip_r from a sidecar, touching only that member."""
    raw = open(path, 'rb').read().decode()
    nl = '\r\n' if '\r\n' in raw else '\n'
    lines = raw.split(nl)
    out = None
    if lines and lines[0].strip() == '{' and len(lines) > 1:
        ind = re.match(r'\s*', lines[1]).group(0)
        for i, ln in enumerate(lines):
            m = re.match(r'^' + re.escape(ind) + r'"escalation_skip_r":\s*(.*)$', ln)
            if not m:
                continue
            j = i
            if m.group(1).startswith('[') and ']' not in m.group(1):
                while j + 1 < len(lines) and ']' not in lines[j]:
                    j += 1
            rest = lines[:i] + lines[j + 1:]
            if not lines[j].rstrip().endswith(','):          # it was the last member: drop the comma before it
                k = i - 1
                while k > 0 and not rest[k].strip():
                    k -= 1
                rest[k] = re.sub(r',\s*$', '', rest[k])
            out = nl.join(rest)
            break
    try:
        ok = out is not None and 'escalation_skip_r' not in json.loads(out)
    except ValueError:
        ok = False
    if not ok:
        doc = json.loads(raw)
        doc.pop('escalation_skip_r', None)
        out = json.dumps(doc, indent=1) + nl
    open(path, 'wb').write(out.encode())


def arm_of(value):
    """Manifest arm fields that pin a given allowance (scalar or per-depth)."""
    return dict(esc_skip_rv=list(value)) if isinstance(value, list) else dict(esc_skip_r=float(value))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('decks', nargs='+', help='deck folders (decks/<Deck> or decks/<Deck>/<variant>)')
    ap.add_argument('--work', help='scratch dir (default logs/esk_stage/<keys>)')
    ap.add_argument('--decisions', help='append every verdict here (valueleaf.sh passes <queue>/DECISIONS.txt)')
    ap.add_argument('--write', action='store_true', help='install an adopted allowance into the live sidecar')
    a = ap.parse_args()

    reg = {os.path.normpath(d.dir): d for d in deck_registry.discover().values()}
    decks = []
    for dd in a.decks:
        d = reg.get(os.path.normpath(dd))
        if d is None:
            sys.exit(f"no registry entry for {dd} (needs <Stem>.cod|.txt AND <Stem>.profile.json)")
        decks.append(d)
    work = a.work or os.path.join('logs', 'esk_stage', '+'.join(sorted(d.key for d in decks)))
    os.makedirs(work, exist_ok=True)
    mtg = engine_bin('mtg')
    notes = []                                       # (key, DECISIONS text)
    report = []

    def existing(d):
        try:
            return json.load(open(d.value)).get('escalation_skip_r')
        except (OSError, ValueError):
            return None

    def keep_note(d):
        e = existing(d)
        return (f"\n    the live sidecar's existing escalation_skip_r = {json.dumps(e)} is KEPT -- it was measured against "
                f"the leaf live when it was set; if this run installed a new leaf it is stale: USER, keep or remove it."
                if e not in (None, 0, 0.0, []) else "")

    live = [d for d in decks if os.path.exists(d.value)]
    for d in decks:
        if d not in live:
            notes.append((d.key, f"PHASE E.6: {d.key} skipped -- no live value sidecar ({d.value})"))
            report.append((d.key, 'skipped: no live leaf'))

    # ---- 1. PROBE
    cand = {}
    if live:
        log, err = run_batch(mtg, work, 'probe', [
            dict(name=f'{d.key}@r0', deck=d.deck_file, profile=d.profile, games=PROBE_GAMES, seed=PROBE_SEED,
                 esc_skip_r=0.0) for d in live])
        g = cal.parse_gdg([err])
        rows = collections.defaultdict(list)
        for job, r in cal.parse_esk([err]):
            rows[job.partition('@')[0]].append(r)
        ms = job_ms(log)
        for d in live:
            units = sum(v[2] for v in g.get(f'{d.key}@r0', {}).values())
            rs = rows.get(d.key, [])
            if not rs:
                notes.append((d.key, f"PHASE E.6: no allowance for {d.key} -- no ladder escalations in {PROBE_GAMES} games "
                                     f"(single-path or trust-dominated); fleet default R = 0" + keep_note(d)))
                report.append((d.key, 'not applicable: no ladder escalations'))
                continue
            vec, ns = cal.fit(rs)
            ceil, proj, take = cal.headroom(rs, units, vec)
            head = (f"probe {PROBE_GAMES} g (seed {PROBE_SEED}): {len(rs)} ladder escalations, headroom {100 * ceil:.1f}% "
                    f"of all work, fitted vector {json.dumps(vec)} (n/depth {'/'.join(map(str, ns))}) projects "
                    f"-{100 * proj:.1f}%")
            if ceil < HEADROOM_MIN or proj <= 0.0:
                why = (f"headroom {100 * ceil:.1f}% < {100 * HEADROOM_MIN:.0f}%" if ceil < HEADROOM_MIN
                       else "the fitted vector skips nothing")
                notes.append((d.key, f"PHASE E.6: no allowance for {d.key} -- {why}; fleet default R = 0\n    {head}"
                                     + keep_note(d)))
                report.append((d.key, f'gave up: {why} (projected -{100 * proj:.1f}%)'))
                continue
            per = ms.get(f'{d.key}@r0', 1000.0)
            cores = len(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else (os.cpu_count() or 8)
            games = int(max(AB_MIN, min(AB_MAX, AB_WALL_S * cores * 1000 / (2 * per))))
            cur = existing(d)
            cand[d.key] = dict(deck=d, vec=vec, head=head, games=games,
                               cur=cur if cur not in (None, 0, 0.0, []) else None)

    # ---- 2. A/B: R = 0 vs the fitted vector, plus the deck's existing allowance when it has one
    if cand:
        jobs = []
        for k, c in cand.items():
            d = c['deck']
            base = dict(deck=d.deck_file, profile=d.profile, games=c['games'], seed=AB_SEED)
            c['arms'] = {'rv': dict(esc_skip_rv=c['vec'])}
            if c['cur'] is not None:
                c['arms']['cur'] = arm_of(c['cur'])
            jobs.append(dict(name=f'{k}@r0', esc_skip_r=0.0, **base))
            jobs += [dict(name=f'{k}@{arm}', **f, **base) for arm, f in c['arms'].items()]
        _, err = run_batch(mtg, work, 'ab', jobs)
        g = cal.parse_gdg([err])
        iso_jobs = []
        for k, c in cand.items():
            d = c['deck']
            b = g.get(f'{k}@r0', {})
            c['res'] = {}
            for arm, f in c['arms'].items():
                o = g.get(f'{k}@{arm}', {})
                gis = sorted(set(b) & set(o))
                r = dict(n=len(gis), units=sum(o[i][2] for i in gis) / max(1, sum(b[i][2] for i in gis)),
                         moved=[i for i in gis if b[i][1] != o[i][1]])
                if len(gis) < c['games']:
                    r['fail'] = f"only {len(gis)} of {c['games']} games paired"
                elif len(r['moved']) > ISO_MAX:
                    r['fail'] = f"{len(r['moved'])} games moved (> {ISO_MAX}: not flicker)"
                c['res'][arm] = r
            # One pair of one-game replays per moved game (the R = 0 replay is shared by both arms).
            need = sorted({i for r in c['res'].values() if 'fail' not in r for i in r['moved']})
            for i in need:
                one = dict(deck=d.deck_file, profile=d.profile, games=1, seed=AB_SEED + i, game_index=i)
                iso_jobs.append(dict(name=f'{k}@i0_{i}', esc_skip_r=0.0, **one))
                iso_jobs += [dict(name=f'{k}@i{arm}_{i}', **f, **one)
                             for arm, f in c['arms'].items() if 'fail' not in c['res'][arm] and i in c['res'][arm]['moved']]
        # ---- 3. ISOLATE
        gi_ = cal.parse_gdg([run_batch(mtg, work, 'iso', iso_jobs)[1]]) if iso_jobs else {}
        for k, c in cand.items():
            d = c['deck']
            W = lambda w: w if w >= 0 else d.max_turns + 1
            for arm, r in c['res'].items():
                real, flicker = [], 0
                for i in ([] if 'fail' in r else r['moved']):
                    x0 = gi_.get(f'{k}@i0_{i}', {}).get(i)
                    x1 = gi_.get(f'{k}@i{arm}_{i}', {}).get(i)
                    if x0 is None or x1 is None:
                        r['fail'] = f"isolation replay of gi{i} missing"
                        break
                    if x0[1] == x1[1]:
                        flicker += 1
                    else:
                        real.append((i, x0[0], x1[0], W(x1[0]) - W(x0[0])))
                slower = [x for x in real if x[3] > 0]
                net = sum(x[3] for x in real)
                r['ok'] = 'fail' not in r and not slower and net <= 0 and r['units'] < 1.0
                r['why'] = (r['fail'] if 'fail' in r else f"{len(slower)} game(s) slower" if slower
                            else f"net {net:+d} turns" if net > 0 else "no unit saving" if r['units'] >= 1.0 else "")
                r['text'] = (f"units {100 * (r['units'] - 1):+.1f}%, {len(r['moved'])} moved: {flicker} flicker, "
                             f"{len(real)} real" + (" (" + ", ".join(f"gi{i} {w0}->{w1}" for i, w0, w1, _ in real)
                                                    + ")" if real else ""))
            rv, cur = c['res']['rv'], c['res'].get('cur')
            ab = (f"A/B {rv['n']} g vs R = 0 (seed {AB_SEED}; no win = {d.max_turns + 1}): fitted {json.dumps(c['vec'])} "
                  f"{rv['text']}" + (f"{'' if rv['ok'] else ' -- FAILS: ' + rv['why']}"))
            if cur is not None:
                ab += (f"\n    existing {json.dumps(c['cur'])}: {cur['text']}"
                       + ('' if cur['ok'] else f" -- FAILS: {cur['why']}"))
            tree = "this tree, NOT committed" if a.write else "NOT applied (run without --write)"
            nxt = "\n    next: commit it with the deck's smoke + regression GT re-accepted (it can change play)."
            if rv['ok'] and (cur is None or not cur['ok'] or rv['units'] < cur['units']):
                if a.write:
                    install(d.value, c['vec'])
                was = f" (replaces {json.dumps(c['cur'])})" if cur is not None else ""
                notes.append((k, f"PHASE E.6: ADOPTED {k} escalation_skip_r = {json.dumps(c['vec'])}{was} -- {tree}\n"
                                 f"    {c['head']}\n    {ab}{nxt}"))
                report.append((k, f"ADOPTED {json.dumps(c['vec'])}{was}: {rv['text']}"))
            elif cur is not None and cur['ok']:
                why = "saves at least as much" if rv['ok'] else f"the fit fails: {rv['why']}"
                notes.append((k, f"PHASE E.6: KEPT {k}'s existing escalation_skip_r = {json.dumps(c['cur'])} -- {why}\n"
                                 f"    {c['head']}\n    {ab}"))
                report.append((k, f"kept existing {json.dumps(c['cur'])} ({why}): {cur['text']}"))
            elif cur is not None:
                if a.write:
                    remove(d.value)
                notes.append((k, f"PHASE E.6: REMOVED {k}'s escalation_skip_r = {json.dumps(c['cur'])} -- it fails the "
                                 f"bar on the live leaf ({cur['why']}) and so does the fit ({rv['why']}); fleet default "
                                 f"R = 0 -- {tree}\n    {c['head']}\n    {ab}{nxt}"))
                report.append((k, f"REMOVED existing {json.dumps(c['cur'])}: {cur['why']}; fit: {rv['why']}"))
            else:
                notes.append((k, f"PHASE E.6: NOT ADOPTED {k} -- {rv['why']}; fleet default R = 0 stands\n"
                                 f"    {c['head']}\n    {ab}"))
                report.append((k, f"not adopted: {rv['why']}; {rv['text']}"))

    with open(os.path.join(work, 'report.txt'), 'w') as f:
        for k, t in notes:
            f.write(f"=== {now()}  {t}\n\n")
    if a.decisions:
        with open(a.decisions, 'a') as f:
            for k, t in notes:
                f.write(f"=== {now()}  {t}\n\n")
    print("[E.6] escalation-skip allowance:")
    for k, t in sorted(report):
        print(f"  {k:28s} {t}")
    print(f"[E.6] details: {os.path.join(work, 'report.txt')}")


if __name__ == '__main__':
    try:
        main()
    except RuntimeError as e:
        print(f"[E.6] FAILED: {e}", file=sys.stderr)
        sys.exit(1)
