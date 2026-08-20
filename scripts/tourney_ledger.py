#!/usr/bin/env python3
"""Regenerate the measured sections of the Mirrorwing card-tournament record.

    python3 scripts/tourney_ledger.py                 # refresh docs/design/mirrorwing-card-tournament-results.md
    python3 scripts/tourney_ledger.py --only runB     # refresh one section

The record is a mix of two things that must not be maintained the same way:

  * MEASUREMENTS -- every option tried, with its numbers. Auto-generated, always straight from the
    run's own stderr, never hand-transcribed. Transcription is where a number silently becomes
    wrong, and this project has already had one conclusion reversed because a figure was quoted
    from memory instead of recomputed.
  * REASONS -- why a card wins, what the case logs showed, what we got wrong. Prose, written by
    hand, and left completely alone by this script.

So the script rewrites ONLY the regions between `<!-- AUTO:name -->` and `<!-- /AUTO:name -->` and
preserves everything else byte-for-byte. Re-run it whenever a batch lands; append prose freely.
"""
import argparse, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import tourney_report as tr  # noqa: E402

DOC = os.path.join(ROOT, "docs", "design", "mirrorwing-card-tournament-results.md")

RUN_A = os.path.join(ROOT, "logs", "tourney", "run_alias", "tourney.err")
RUN_B = os.path.join(ROOT, "logs", "tourney", "run_B", "tourney.err")

TF = ["tf3lib0", "tf2lib1", "tf1lib2", "tf0lib3"]
TFN = {"tf3lib0": "3 Twinflame", "tf2lib1": "2 Twin / 1 Lib",
       "tf1lib2": "1 Twin / 2 Lib", "tf0lib3": "3 Libation"}

# Run A -- alias map A (Oracle -> Anger's cap-4 bucket, Draught -> Scale's cap-2 bucket)
A_COMBO = [("a4o0", "scale", "4 Anger + 2 Scale"), ("a4o0", "draught", "4 Anger + 2 Draught"),
           ("a2o2", "scale", "2 Anger/2 Oracle + 2 Scale"),
           ("a2o2", "draught", "2 Anger/2 Oracle + 2 Draught"),
           ("a0o4", "scale", "4 Oracle + 2 Scale"), ("a0o4", "draught", "4 Oracle + 2 Draught")]
# Run B -- alias map B (Draught -> Anger's cap-4 bucket, Oracle -> Scale's cap-2 bucket)
B_COMBO = [("a4s2", "4 Anger + 2 Scale"), ("a4o2", "4 Anger + 2 Oracle"),
           ("a2d2o2", "2 Anger + 2 Draught + 2 Oracle"), ("d4s2", "4 Draught + 2 Scale"),
           ("d4o2", "4 Draught + 2 Oracle")]

HDR = ("| option | life | games | mean turn | vs baseline | t | better | worse | margin | s |\n"
       "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|")


def row(label, life, s, mean):
    return (f"| {label} | {life} | {s['n']:,} | {mean:.4f} | {s['md']:+.4f} | {s['t']:+.1f} | "
            f"{s['better']:,} | {s['worse']:,} | **{s['margin']:+,}** | {s['sigma']:+.1f} |")


def combo_table(data, games, base_arms, combos, arm_of, lives=(20, 30)):
    out = [HDR]
    for life in lives:
        for key in combos:
            label = key[-1]
            aa = [f"{arm_of(base_arms, t)}@L{life}" for t in TF]
            bb = [f"{arm_of(key, t)}@L{life}" for t in TF]
            if any(x not in data for x in aa + bb):
                continue
            if aa == bb:
                m = sum(sum(data[a][0]) / games for a in aa) / len(aa)
                out.append(f"| **{label}** (baseline) | {life} | {games * len(aa):,} | {m:.4f} "
                           f"| -- | -- | -- | -- | -- | -- |")
                continue
            s = tr.contrast(data, aa, bb, games)
            out.append(row(label, life, s, s["mean_b"]))
    return "\n".join(out)


def ladder_table(data, games, arm_of, fixed, lives=(20, 30)):
    """Twinflame -> Libation dose response, pooled over the trick configurations."""
    out = ["| step | life | games | turns | t | margin | s |", "|---|---:|---:|---:|---:|---:|---:|"]
    for life in lives:
        for i in range(len(TF) - 1):
            for j in range(i + 1, len(TF)):
                aa = [f"{arm_of(k, TF[i])}@L{life}" for k in fixed]
                bb = [f"{arm_of(k, TF[j])}@L{life}" for k in fixed]
                if any(x not in data for x in aa + bb):
                    continue
                s = tr.contrast(data, aa, bb, games)
                out.append(f"| {TFN[TF[i]]} -> {TFN[TF[j]]} | {life} | {s['n']:,} | "
                           f"{s['md']:+.4f} | {s['t']:+.1f} | **{s['margin']:+,}** | "
                           f"{s['sigma']:+.1f} |")
    return "\n".join(out)


def bridge_table(dA, dB, games):
    """The unaliased baseline, measured under BOTH alias maps on the same seeds.

    It holds no aliased card, so the two runs should agree game-for-game. If they do not, the two
    runs cannot be chained through it and every cross-map statement in this document is void."""
    out = ["| arm | life | games | run A mean | run B mean | identical games |",
           "|---|---:|---:|---:|---:|:--:|"]
    for life in (20, 30):
        for a, b in (("tf3lib0_a4o0_scale", "tf3lib0_a4s2"),):
            ka, kb = f"{a}@L{life}", f"{b}@L{life}"
            if ka not in dA or kb not in dB:
                continue
            wa, wb = dA[ka][0], dB[kb][0]
            same = sum(1 for i in range(games) if wa[i] == wb[i])
            out.append(f"| {b} | {life} | {games:,} | {sum(wa)/games:.4f} | {sum(wb)/games:.4f} | "
                       f"{'YES' if same == games else f'NO ({same:,}/{games:,})'} |")
    return "\n".join(out)


def splice(text, name, body):
    a, b = f"<!-- AUTO:{name} -->", f"<!-- /AUTO:{name} -->"
    pat = re.compile(re.escape(a) + r".*?" + re.escape(b), re.S)
    if not pat.search(text):
        sys.exit(f"marker {a} not found in the document -- refusing to guess where it goes")
    return pat.sub(a + "\n" + body + "\n" + b, text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--games", type=int, default=10000)
    ap.add_argument("--only", default="")
    a = ap.parse_args()
    if not os.path.exists(DOC):
        sys.exit(f"{DOC} does not exist -- write the prose scaffold first")
    text = open(DOC).read()

    def load(path, label):
        """An IN-FLIGHT run is absent, not a failure. tr.load refuses a partial batch on purpose --
        that guard is what stops a half-finished run being read as a result -- so here it just means
        'not ready yet' and the document keeps its previous content for that section."""
        if not os.path.exists(path):
            return {}
        try:
            return tr.load(path, a.games, 8)
        except SystemExit as e:
            print(f"  {label}: not ready ({str(e).split(':')[-1].strip()[:60]}...) -- section left "
                  f"as-is")
            return {}

    dA = load(RUN_A, "run A")
    dB = load(RUN_B, "run B")

    if dA and a.only in ("", "runA"):
        text = splice(text, "runA", combo_table(
            dA, a.games, ("a4o0", "scale", ""), A_COMBO,
            lambda k, t: f"{t}_{k[0]}_{k[1]}"))
        text = splice(text, "runA_ladder", ladder_table(
            dA, a.games, lambda k, t: f"{t}_{k[0]}_{k[1]}",
            [(x, y) for x in ("a4o0", "a2o2", "a0o4") for y in ("scale", "draught")]))
    if dB and a.only in ("", "runB"):
        text = splice(text, "runB", combo_table(
            dB, a.games, ("a4s2", ""), B_COMBO, lambda k, t: f"{t}_{k[0]}"))
        text = splice(text, "runB_ladder", ladder_table(
            dB, a.games, lambda k, t: f"{t}_{k[0]}", [(c[0],) for c in B_COMBO]))
    if dA and dB and a.only in ("", "bridge"):
        text = splice(text, "bridge", bridge_table(dA, dB, a.games))

    open(DOC, "w").write(text)
    print(f"refreshed {os.path.relpath(DOC, ROOT)}"
          f"  (run A: {'yes' if dA else 'absent'}, run B: {'yes' if dB else 'absent'})")


if __name__ == "__main__":
    main()
