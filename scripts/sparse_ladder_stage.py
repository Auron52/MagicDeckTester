#!/usr/bin/env python3
"""Value-leaf pipeline phase E.8: replace the HEURISTIC ROLLOUT LADDER with the SPARSE HEURISTIC LADDER.

    python3 scripts/sparse_ladder_stage.py decks/<Deck> [decks/<Deck2> ...] [--work DIR] [--decisions FILE] [--write]
                                   [--seed-base N] [--blocks N] [--games N]

Normally run by `bash scripts/valueleaf.sh run` (last, after E.6) or alone by `bash scripts/valueleaf.sh sparse
[decks/<Deck> ...]`.

WHY. USER 2026-11-17 (ruling 3 on P18j): "valueleaf.sh GAINS A STAGE that measures mode 4 against the heur ladder
(when the ladder is the shape that would ship) and turns it on for the deck when it wins." USER 2026-11-19: "I don't
want the heuristic ladder for any deck." USER 2026-10-24: "It should be possible to replace an expensive rollout
ladder with a leafless shape or equivalent up to the final rollout. Doing early rollouts for little to no benefit
makes no sense." USER 2026-11-19: "The idea is that I want to actually skip some of the rollouts we don't need."
The sparse heuristic ladder (`value_play.ladder = "sparse"`, = sampled gate mode 4 in TurnSolver.cpp p18::Knobs;
research name P18j) is the WIN-TRIGGERED leafless
lookahead: one capped rollout of the root, and when it wins inside the window, leafless passes to the depth that win
needs, committing the first proven in-window win; a miss refunds its units and priced work and the ordinary ladder
continues on exactly its own budget. Design and history: docs/design/sparse-heuristic-ladder.md.

WHICH DECKS: the ones whose live shape is a HEURISTIC-LADDER shape (docs/design/sparse-heuristic-ladder.md,
"Which shapes run the heuristic ladder"): no sidecar, a play-settings-only sidecar (sampled gate modes 1-3 included:
their fallback is the ladder), and the emulated ladder. The leafless shapes (`leaf: none` single / escalation) and a
learned value leaf are reported and skipped. A deck already on "sparse" is skipped.

WHAT IT MEASURES. ONE pooled batch: per deck `live` (the shipped sidecar, or none) and `sparse` (the same file with
value_play.ladder = "sparse" and the keys of the shape it replaces dropped -- sampled_gate, leaf, commit, emul_r; any
learned model in the file is set aside by the loader; a play-settings-only `{"value_play": {"ladder": "sparse"}}`
sidecar when the deck has none), each in a variant folder whose other files are symlinks (so both arms share the keep table's cache entry), on
--blocks held-out seeds x --games paired games, the arms adjacent in the queue. Per-game win turns come from the
batch's .wins files, so every earlier / later game is listed.

WHAT IT DECIDES. ADOPT iff the sparse ladder costs LESS in work units (never wall-clock) AND its paired quality is not worse at
all: the sum over paired games of (sparse turn - live turn), unwon = max_turns + 1, is <= 0 ("+1 is not neutral. 0 is
neutral."). Anything else is NOT adopted and is recorded as a RESIDUAL LOSER to investigate -- the design goal is
that the sparse ladder beats the ladder on every deck (USER 2026-11-17: "If there are cases where the mode loses to
the heuristic ladder we should keep investigating."), so a loss is a defect in the shape, not an accepted outcome.

--write installs an adopted sparse sidecar IN THIS TREE (never committed here: it changes play, so the deck's smoke +
regression GT must be re-accepted with it). Every outcome is appended to --decisions; nothing stops to ask. Exit 0
when the batch ran (whatever the verdicts), 1 when it failed (nothing is installed then).
"""
import argparse, datetime, json, math, os, re, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import deck_registry                                   # noqa: E402
import esc_skip_stage as esk                           # noqa: E402  (inputs_fp: the resume fingerprint)
from engine_bin import engine_bin, kind                # noqa: E402

# Held out from every other stage: phase E (600000..), the matrix (8008..), E.6 (7.1M / 7.3M), E.7 (7.505M..7.576M),
# and every P18 screen (7.0M..7.55M F4, 8.0M F5, 9M..16M P18i/P18j).
SEED_BASE = 7_705_000
BLOCKS = 8
GAMES = 500
LINE = re.compile(r"^(\S+)_s(\d+): played=(\d+) avg=([\d.]+) digest=(\w+)(?: ms=(\d+))?(?: units=(\d+))?")


def now():
    return datetime.datetime.now().strftime('%Y-%m-%dT%H:%M:%S')


SHAPE_KEYS = ('sampled_gate', 'leaf', 'commit', 'emul_r')     # the replaced shape's keys, dropped from the sparse arm


def shape_of(doc):
    """'bare' (no sidecar / play-only, sampled gate modes included), 'emulated', 'sparse', 'leafless', 'learned'."""
    vp = (doc or {}).get('value_play') or {}
    if vp.get('ladder') == 'sparse' and model_kind(doc) != 'constant':
        return 'sparse'
    if vp.get('ladder') == 'emulated':
        return 'emulated'
    mk = model_kind(doc)
    return {'none': 'bare', 'constant': 'leafless', 'learned': 'learned'}[mk]


def model_kind(doc):
    """'none' (no model attached: the heuristic ladder), 'constant' (leaf none), 'learned'. Mirrors
    MulliganProfileIO.h AttachValueSidecar + EvalModelFromJsonObj."""
    if doc is None:
        return 'none'
    vp = doc.get('value_play') or {}
    if vp.get('leaf') == 'none' and vp.get('commit') != 'model':
        return 'constant'
    m = doc.get('eval_model', doc)
    learned = (isinstance(m.get('coefs'), dict) and bool(m.get('coefs'))) or (isinstance(m.get('trees'), list) and bool(m.get('trees')))
    return 'learned' if learned else 'none'


def sparse_doc(live_doc):
    doc = dict(live_doc or {})
    vp = {k: v for k, v in (doc.get('value_play') or {}).items() if k not in SHAPE_KEYS}
    vp['ladder'] = 'sparse'
    doc['value_play'] = vp
    return doc


def variant(deck, arm, doc, root):
    """A deck folder differing from the real one ONLY in its value.json (absent when doc is None)."""
    dest = os.path.join(root, deck.key, arm)
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    os.makedirs(dest)
    for f in os.listdir(deck.dir):
        src = os.path.join(deck.dir, f)
        if f == deck.stem + ".value.json" or not os.path.isfile(src):
            continue
        os.symlink(os.path.realpath(src), os.path.join(dest, f))
    if doc is not None:
        with open(os.path.join(dest, deck.stem + ".value.json"), "w") as fo:
            json.dump(doc, fo)
    return os.path.join(dest, os.path.basename(deck.deck_file)), os.path.join(dest, deck.stem + ".profile.json")


def run_batch(mtg, work, label, jobs):
    """ONE pooled batch with per-game .wins; a finished one with unchanged manifest and inputs is reused."""
    man = os.path.join(work, label + '.manifest.json')
    log, err = os.path.join(work, label + '.log'), os.path.join(work, label + '.err')
    fpf = os.path.join(work, label + '.inputs.sha256')
    wins = os.path.join(work, label + '.wins')
    text = json.dumps({'jobs': jobs}, indent=1)
    fp = esk.inputs_fp(mtg, jobs)
    if (os.path.exists(man) and open(man).read() == text and os.path.exists(fpf)
            and open(fpf).read().strip() == fp and os.path.exists(log)
            and '=== BATCH done' in open(log, errors='replace').read()):
        print(f"[E.8 sparse] {label}: reusing the finished batch in {work} (inputs fingerprint {fp[:12]} unchanged)", flush=True)
        return log, wins
    if os.path.exists(fpf):
        os.remove(fpf)
    open(man, 'w').write(text)
    print(f"[E.8 sparse] {label}: {len(jobs)} jobs, {sum(j['games'] for j in jobs)} games ({mtg}, {kind(mtg)})", flush=True)
    with open(log, 'w') as lo, open(err, 'w') as er:
        rc = subprocess.run([mtg, '--batch', man, '--threads', '0', '--game-log-dir', wins],
                            stdout=lo, stderr=er).returncode
    if rc != 0 or '=== BATCH done' not in open(log, errors='replace').read():
        raise RuntimeError(f"{label} batch failed (exit {rc}; 137 = SIGKILL, think OOM) -- see {err}")
    open(fpf, 'w').write(fp + '\n')
    return log, wins


def read_wins(path, loss):
    out = {}
    for ln in open(path):
        p = ln.split()
        if len(p) >= 2:
            w = int(p[1])
            out[int(p[0])] = (w if w > 0 else loss, p[2] if len(p) > 2 else '')
    return out


def compare(key, seeds, lines, wins_dir, loss):
    """Paired per-game comparison live vs sparse over every seed block."""
    jl = {}
    for ln in lines:
        m = LINE.match(ln.strip())
        if m and m.group(1).startswith(key + '-'):
            jl[(m.group(1)[len(key) + 1:], int(m.group(2)))] = dict(ms=int(m.group(6) or 0), units=int(m.group(7) or 0),
                                                                    digest=m.group(5))
    tot = dict(live_ms=0, sp_ms=0, live_u=0, sp_u=0)
    diffs, moved, ident = [], [], 0
    for s in seeds:
        a, b = jl.get(('live', s)), jl.get(('sparse', s))
        if a is None or b is None:
            raise RuntimeError(f"{key}: missing job line for seed {s}")
        tot['live_ms'] += a['ms']; tot['sp_ms'] += b['ms']; tot['live_u'] += a['units']; tot['sp_u'] += b['units']
        wa = read_wins(os.path.join(wins_dir, f"{key}-live_s{s}.wins"), loss)
        wb = read_wins(os.path.join(wins_dir, f"{key}-sparse_s{s}.wins"), loss)
        for gi in sorted(set(wa) & set(wb)):
            d = wb[gi][0] - wa[gi][0]
            diffs.append(d)
            ident += wa[gi][1] == wb[gi][1]
            if d != 0:
                moved.append((s, gi, wa[gi][0], wb[gi][0]))
    n = len(diffs)
    mean = sum(diffs) / n if n else 0.0
    sd = math.sqrt(sum((x - mean) ** 2 for x in diffs) / (n - 1)) if n > 1 else 0.0
    t = mean / (sd / math.sqrt(n)) if sd > 0 else 0.0
    return dict(n=n, net=sum(diffs), mean=mean, t=t, earlier=sum(1 for x in diffs if x < 0),
                later=sum(1 for x in diffs if x > 0), moved=moved, ident=ident,
                units=(tot['sp_u'] / tot['live_u']) if tot['live_u'] else float('nan'),
                cpu=(tot['sp_ms'] / tot['live_ms']) if tot['live_ms'] else float('nan'), **tot)


def install(path, doc):
    raw = open(path).read() if os.path.exists(path) else '{\n'
    out = json.dumps(doc, indent=1 if raw.count("\n") > 2 else None) + "\n"
    open(path, "w").write(out)
    if json.load(open(path)).get("value_play") != doc["value_play"]:
        raise RuntimeError(f"{path}: value_play did not round-trip")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('decks', nargs='+', help='deck folders (decks/<Deck>)')
    ap.add_argument('--work', help='scratch dir (default logs/sparse_stage/<keys>)')
    ap.add_argument('--decisions', help='append every verdict here (valueleaf.sh passes <queue>/DECISIONS.txt)')
    ap.add_argument('--write', action='store_true', help='install an adopted sparse sidecar into the deck folder')
    ap.add_argument('--seed-base', type=int, default=SEED_BASE)
    ap.add_argument('--blocks', type=int, default=BLOCKS)
    ap.add_argument('--games', type=int, default=GAMES)
    a = ap.parse_args()

    reg = {os.path.normpath(d.dir): d for d in deck_registry.discover().values()}
    decks = []
    for dd in a.decks:
        d = reg.get(os.path.normpath(dd))
        if d is None:
            sys.exit(f"no registry entry for {dd} (needs a decklist AND a profile)")
        decks.append(d)
    work = a.work or os.path.join('logs', 'sparse_stage', '+'.join(sorted(d.key for d in decks)))
    os.makedirs(work, exist_ok=True)
    vroot = os.path.join(work, 'variants')
    mtg = engine_bin('mtg')
    seeds = [a.seed_base + 10_000 * i for i in range(a.blocks)]
    notes, report, plan = [], [], {}

    for d in decks:
        live_doc = json.load(open(d.value)) if os.path.exists(d.value) else None
        shape = shape_of(live_doc)
        sg = ((live_doc or {}).get('value_play') or {}).get('sampled_gate', 0)
        if shape in ('leafless', 'learned', 'sparse'):
            notes.append(f"PHASE E.8: {d.key} skipped -- its live shape is {shape}, not a heuristic-ladder shape")
            report.append((d.key, f'skipped: {shape} shape'))
            continue
        plan[d.key] = dict(deck=d, live_doc=live_doc, sg=sg, shape=shape, live=variant(d, 'live', live_doc, vroot),
                           sparse=variant(d, 'sparse', sparse_doc(live_doc), vroot), doc=sparse_doc(live_doc),
                           loss=(d.max_turns or 8) + 1)

    if plan:
        jobs = []
        for s in seeds:
            for k, p in plan.items():
                for arm in ('live', 'sparse'):                  # arms adjacent: both run under the same contention
                    jobs.append(dict(name=f"{k}-{arm}_s{s}", deck=p[arm][0], profile=p[arm][1], games=a.games,
                                     seed=s, max_turns=p['loss'] - 1))
        log, wins = run_batch(mtg, work, 'sparse', jobs)
        lines = open(log, errors='replace').read().splitlines()
        for k, p in plan.items():
            d = p['deck']
            r = compare(k, seeds, lines, wins, p['loss'])
            adopt = r['units'] < 1.0 and r['net'] <= 0 and r['ident'] < r['n']
            base = {'bare': f"sampled gate mode {p['sg']}" if p['sg'] else "the bare heuristic ladder",
                    'emulated': "the emulated ladder"}[p['shape']]
            head = (f"{k}: live = {base} ({'no sidecar' if p['live_doc'] is None else 'sidecar'}); {len(seeds)} seeds x "
                    f"{a.games} paired games (seeds {seeds[0]}..): net {r['net']:+d} turns (mean {r['mean']:+.5f}, "
                    f"t {r['t']:+.2f}), {r['earlier']} earlier / {r['later']} later, units {r['units']:.4f}x, "
                    f"cpu {r['cpu']:.4f}x, {r['ident']}/{r['n']} digest-identical")
            mv = "".join(f"\n      seed {s} gi {gi}: T{x} -> T{y}" for s, gi, x, y in r['moved'] if y > x)
            if adopt:
                if a.write:
                    install(d.value, p['doc'])
                tree = "installed in this tree, NOT committed" if a.write else "NOT applied (run without --write)"
                notes.append(f"PHASE E.8: ADOPTED the sparse ladder for {head} -- {tree}{mv}\n"
                             f"    next: commit it with the deck's smoke + regression GT re-accepted (it changes play).")
                report.append((k, f"ADOPTED sparse (net {r['net']:+d}, units {r['units']:.3f}x, {r['earlier']}/{r['later']})"))
            else:
                why = []
                if r['units'] >= 1.0:
                    why.append(f"not cheaper (units {r['units']:.4f}x)")
                if r['net'] > 0:
                    why.append(f"worse quality (net {r['net']:+d} turns)")
                if r['ident'] == r['n']:
                    why.append("byte-identical (the sparse ladder never engaged)")
                notes.append(f"PHASE E.8: NOT ADOPTED {head} -- {'; '.join(why)}. RESIDUAL LOSER: root-cause it "
                             f"(USER 2026-11-17: a deck where the sparse ladder loses to the ladder is investigated, not accepted){mv}")
                report.append((k, f"NOT adopted ({'; '.join(why)})"))

    with open(os.path.join(work, 'report.txt'), 'w') as f:
        for t in notes:
            f.write(f"=== {now()}  {t}\n\n")
    if a.decisions:
        with open(a.decisions, 'a') as f:
            for t in notes:
                f.write(f"=== {now()}  {t}\n\n")
    print("[E.8] the sparse heuristic ladder vs the heuristic-ladder shape that ships:")
    for k, t in sorted(report):
        print(f"  {k:28s} {t}")
    print(f"[E.8 sparse] details: {os.path.join(work, 'report.txt')}")


if __name__ == '__main__':
    try:
        main()
    except RuntimeError as e:
        print(f"[E.8 sparse] FAILED: {e}", file=sys.stderr)
        sys.exit(1)
