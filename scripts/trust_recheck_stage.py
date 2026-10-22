#!/usr/bin/env python3
"""Value-leaf pipeline phase E.7b: re-run the TRUST acceptance when phase E.7 moved the play setting.

    python3 scripts/trust_recheck_stage.py decks/<Deck> [...] --staged KEY=logs/eval/<stem>.value.STAGED.json [...]
                                           [--work DIR] [--decisions FILE] [--write]

WHY (USER 2026-10-22: "Do we need to make any other changes in line with what you mentioned?"). Phase E decides
trust (vlq_trust_accept.py: ON vs OFF, non-inferiority) at the staged model's play setting; E.7 runs later and can
move that setting. Trust is a depth question -- value_trust_depth spares unverified leaf lines committed at or
above it, and which lines exist depends on the depth and budget played -- so a verdict taken at the old setting does
not describe the new one. Vampires 2026-10-22 is the example: the d5 candidate was rejected at d5 / 20 as never
engaged, then E.7 moved play to d6 (re-checked by hand at d6 / 20: still never engaged).

WHAT IT DOES. For each deck whose live block now differs from the staged sidecar's setting (the one phase E played)
and that carries a trust candidate (value_trust_depth_candidate) or a trust depth: ONE pooled batch, phase E's
trust arms on the live sidecar -- OFF = live without value_trust_depth, ON = live with it at the candidate (or the
current trust depth) -- on phase E's trust seeds, judged by vlq_trust_accept.py unchanged. Accepted -> the live
sidecar gets that value_trust_depth; rejected -> it has none (failing safe: every unverified line stays eligible to
escalate). With --write the result is installed IN THIS TREE (not committed); every outcome goes to --decisions.
Runs BEFORE E.6, because the skip allowance is a measurement of the probe that ships.
Exit 0 when the batch ran (whatever the verdicts), 1 when it failed (nothing is installed then).
"""
import argparse, datetime, json, os, re, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import deck_registry                                   # noqa: E402
import play_setting_stage as pss                       # noqa: E402  (current, variant, run_batch, install)
from engine_bin import engine_bin                      # noqa: E402

SEEDS = [620000 + 1000 * i for i in range(8)]          # valueleaf.sh TRUST_SEEDS: the same held-out games
GAMES = 1000                                           # valueleaf.sh TRUST_GAMES
TOL = 0.002


def now():
    return datetime.datetime.now().strftime('%Y-%m-%dT%H:%M:%S')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('decks', nargs='+')
    ap.add_argument('--staged', action='append', default=[], metavar='KEY=FILE',
                    help="the staged sidecar phase E measured (its setting is the one trust was decided at)")
    ap.add_argument('--work', help='scratch dir (default logs/trust_recheck/<keys>)')
    ap.add_argument('--decisions')
    ap.add_argument('--write', action='store_true')
    a = ap.parse_args()
    staged = dict(x.split('=', 1) for x in a.staged)

    reg = {os.path.normpath(d.dir): d for d in deck_registry.discover().values()}
    decks = [reg.get(os.path.normpath(x)) for x in a.decks]
    if None in decks:
        sys.exit(f"no registry entry for one of {a.decks}")
    work = a.work or os.path.join('logs', 'trust_recheck', '+'.join(sorted(d.key for d in decks)))
    os.makedirs(work, exist_ok=True)
    vroot = os.path.join(work, 'variants')
    notes, plan = [], {}
    for d in decks:
        if not os.path.exists(d.value) or d.key not in staged or not os.path.exists(staged[d.key]):
            continue
        live, st = json.load(open(d.value)), json.load(open(staged[d.key]))
        now_set, then_set = pss.current(live.get('value_play') or {})[:2], pss.current(st.get('value_play') or {})[:2]
        depth = live.get('value_trust_depth') or live.get('value_trust_depth_candidate')
        if now_set == then_set or not depth:
            continue
        off = {k: v for k, v in live.items() if k != 'value_trust_depth'}
        plan[d.key] = dict(deck=d, live=live, depth=int(depth), now=now_set, then=then_set,
                           off=pss.variant(d, 'trustOFF', off, vroot),
                           on=pss.variant(d, 'trustON', dict(off, value_trust_depth=int(depth)), vroot))
    if plan:
        jobs = []
        for k, p in plan.items():
            for s in SEEDS:
                jobs.append(dict(name=f"{k}-trustON_s{s}", deck=p['on'][0], profile=p['on'][1], games=GAMES, seed=s))
                jobs.append(dict(name=f"{k}-trustOFF_s{s}", deck=p['off'][0], profile=p['off'][1], games=GAMES, seed=s))
        log = pss.run_batch(engine_bin('mtg'), work, 'trust', jobs)
        lines = open(log, errors='replace').read().splitlines()
        for k, p in plan.items():
            d = p['deck']
            mlog = os.path.join(work, f"m_{k}_trust.log")
            with open(mlog, 'w') as fo:
                fo.writelines(ln[len(k) + 1:] + "\n" for ln in lines if ln.startswith(f"{k}-") and ': played=' in ln)
            # vlq_trust_accept reads the depth under test from value_trust_depth_candidate; name it explicitly
            # (a live trust depth need not carry the candidate key).
            model = os.path.join(work, f"{d.stem}.trust_model.json")
            json.dump(dict(p['live'], value_trust_depth_candidate=p['depth']), open(model, 'w'))
            r = subprocess.run([sys.executable, os.path.join(HERE, 'vlq_trust_accept.py'), mlog, model,
                                '--tol', str(TOL)], capture_output=True, text=True)
            if r.returncode != 0 or re.search(r'trust: (SKIP|no candidate)', r.stdout):
                raise RuntimeError(f"vlq_trust_accept gave no verdict for {k}:\n{r.stdout}{r.stderr}")
            accepted = re.search(r'trust: ACCEPTED', r.stdout) is not None
            if not accepted and not re.search(r'trust: REJECTED', r.stdout):
                raise RuntimeError(f"vlq_trust_accept printed neither ACCEPTED nor REJECTED for {k}:\n{r.stdout}")
            had = p['live'].get('value_trust_depth')
            new = dict(p['live'])
            if accepted:
                new['value_trust_depth'] = p['depth']
            else:
                new.pop('value_trust_depth', None)
            changed = new.get('value_trust_depth') != had
            if changed and a.write:
                pss.install(d.value, new)
            verdict = (f"trust {'ACCEPTED' if accepted else 'REJECTED'} at d{p['depth']}"
                       f" -> value_trust_depth {new.get('value_trust_depth') or 'unset'}"
                       f" ({'was ' + str(had or 'unset') + ('; installed in this tree, NOT committed' if a.write else '; NOT applied') if changed else 'unchanged'})")
            body = "\n    ".join(l for l in r.stdout.splitlines() if l.strip())
            notes.append(f"PHASE E.7b: {k} -- {verdict}; play moved d{p['then'][0]}/{p['then'][1]} -> "
                         f"d{p['now'][0]}/{p['now'][1]} after phase E decided trust\n    {body}"
                         + ("\n    next: commit it with the deck's smoke + regression GT re-accepted (it changes play)."
                            if changed else ""))
            print(f"[E.7b] {k}: {verdict}")
    else:
        print("[E.7b] no deck whose play setting moved after phase E and that has a trust candidate -- nothing to re-check")
    for path in [os.path.join(work, 'report.txt')] + ([a.decisions] if a.decisions else []):
        with open(path, 'a') as f:
            for t in notes:
                f.write(f"=== {now()}  {t}\n\n")


if __name__ == '__main__':
    try:
        main()
    except RuntimeError as e:
        print(f"[E.7b] FAILED: {e}", file=sys.stderr)
        sys.exit(1)
