#!/usr/bin/env python3
"""Root-cause classifier for paired base-vs-arm game logs.

Answers, per regression, the question that decides WHO OWNS IT -- and refuses to answer it by
inference. USER, 2026-08-25: "Can you also root-source every case that got worse before passing them
on? I don't want to waste the other agent's time if they aren't related to the payment processing."
"We should be doing this exhaustively anyway."

Three checks, in order, because each one invalidates the next if it fires:

  1. MULLIGAN DIVERGENCE. If the kept hands differ, the game diverged BEFORE play and no play
     decision is attributable at all. This is not hypothetical: bottoming consults the search, so a
     search lever changes which cards go to the bottom and the two arms play different games. Two of
     the first three "payment candidates" found by an earlier, cruder version of this script were
     actually this.
  2. PAYMENT TEST. For a cast the baseline made and the arm did not, ask whether the arm ENDED that
     turn with untapped sources able to pay for it. Untapped-and-uncast is a genuine payment
     candidate (the engine wanted it, could pay, and did not). Tapped-out is a mana ALLOCATION
     choice, which belongs to whoever owns the lever, not to the payment solver.
  3. CHOICE. Anything else: the arm picked a different line with the mana available to it.

REPRO FIDELITY IS CHECKED, NOT ASSUMED. A batch game reproduces as
`--seed (base+gi) --game-index gi`; dropping --game-index silently produces a DIFFERENT game that
still looks plausible, which invalidated a whole first pass of this analysis. --verify compares each
reproduced log against the batch .wins entry and refuses to classify on a mismatch.
"""
import json
import pathlib
import sys

CARDS = None


def cost_of(name):
    """-> (generic, coloured_pips) for a card's printed mana cost."""
    global CARDS
    if CARDS is None:
        raw = json.load(open("src/cards/data/cards.json"))
        cards = raw["cards"] if isinstance(raw, dict) and "cards" in raw else raw
        CARDS = {c["name"]: c for c in cards}
    mc = (CARDS.get(name) or {}).get("mana_cost", "") or ""
    gen, pips = 0, 0
    for tok in mc.replace("}", "").split("{"):
        if not tok:
            continue
        if tok.isdigit():
            gen += int(tok)
        else:
            pips += 1
    return gen, pips


def read(path):
    g = json.load(open(path))
    keep = [c["cardName"] for c in g.get("openingHand", [])]
    mulls = len(g.get("mulliganSequence", []) or [])
    acts, draws, board = {}, {}, {}
    for t in g.get("turns", []):
        n = t.get("turn")
        for a in t.get("actions", []):
            ty = a.get("type")
            if ty == "DRAW":
                draws.setdefault(n, []).append(a.get("cardName", ""))
            else:
                acts.setdefault(n, []).append((ty, a.get("cardName", "")))
        if t.get("boardAfter"):
            board[n] = t["boardAfter"]
    return dict(win=win_turn(g), keep=keep, mulls=mulls,
                acts=acts, draws=draws, board=board)


def win_turn(g):
    """THE metric's turn for a logged game: an UNWON game logs `"turn": null` and scores
    max_turns+1 = 9, exactly as the batch's .wins encodes it (-1 -> 9). Reading the raw null
    made --verify report a false REPRO MISMATCH on every unwon game, which reads as an
    unfaithful repro and blocks classification -- the one thing this script exists to prevent
    being done by inference."""
    t = (g.get("result") or {}).get("turn")
    return 9 if t is None else t


def untapped_sources(board):
    """Count untapped lands + untapped mana creatures on our battlefield."""
    if not board:
        return 0
    n = 0
    for p in board.get("battlefield", []):
        if p.get("tapped"):
            continue
        nm = p.get("cardName", "")
        if p.get("isLand"):
            n += 1
        elif CARDS and (CARDS.get(nm, {}).get("parameters", {}).get("mana_rock")
                        or "Add {G}" in (CARDS.get(nm, {}).get("oracle_text", "") or "")):
            n += 1
    return n


def sole(d):
    f = sorted(pathlib.Path(d).glob("*.json"))
    return f[0] if f else None


def classify(tag, bdir, adir):
    bf, af = sole(bdir), sole(adir)
    if not bf or not af:
        return None
    b, a = read(bf), read(af)

    if b["keep"] != a["keep"] or b["mulls"] != a["mulls"]:
        return dict(tag=tag, bw=b["win"], aw=a["win"], turn=None,
                    kind="MULLIGAN divergence (pre-play)",
                    note=f'{b["mulls"]} vs {a["mulls"]} mulligans; kept hands differ')

    for n in sorted(set(b["acts"]) | set(a["acts"])):
        ba, aa = b["acts"].get(n, []), a["acts"].get(n, [])
        if ba == aa and b["draws"].get(n, []) == a["draws"].get(n, []):
            continue
        bc = sorted(c for ty, c in ba if ty == "CAST_SPELL")
        ac = sorted(c for ty, c in aa if ty == "CAST_SPELL")
        missing = [c for c in bc if c not in ac]
        extra = [c for c in ac if c not in bc]
        if missing and not extra:
            # A card the arm casts LATER in the same game is not a payment failure by construction:
            # the engine demonstrably can pay for it. It is a TIMING choice, and it belongs to
            # whoever owns the lever. This distinction matters -- the single "payment candidate" in
            # the StompySurprise sweep was exactly this shape (the arm cast the missing Worldly
            # Tutor twice, two turns later), so the naive test would have handed a lever's own
            # valuation change to the mana-payment owner.
            later = {c for turn, acts in a["acts"].items() if turn > n
                     for ty, c in acts if ty == "CAST_SPELL"}
            deferred = [c for c in missing if c in later]
            if deferred:
                return dict(tag=tag, bw=b["win"], aw=a["win"], turn=n,
                            kind="cast TIMING choice (deferred)",
                            note=f'{deferred} cast later, not unaffordable')
            free = untapped_sources(a["board"].get(n))
            need = min(sum(cost_of(m)) for m in missing)
            if free >= need:
                return dict(tag=tag, bw=b["win"], aw=a["win"], turn=n,
                            kind="PAYMENT candidate",
                            note=f'arm ended T{n} with {free} untapped source(s), '
                                 f'{missing} needs {need}, never cast')
            return dict(tag=tag, bw=b["win"], aw=a["win"], turn=n,
                        kind="mana ALLOCATION choice",
                        note=f'arm tapped out ({free} free, {missing} needs {need})')
        if bc == ac:
            return dict(tag=tag, bw=b["win"], aw=a["win"], turn=n,
                        kind="same casts, sequencing/activation differs", note="")
        return dict(tag=tag, bw=b["win"], aw=a["win"], turn=n, kind="different cast CHOICE",
                    note=(f"missing={missing} " if missing else "") + (f"extra={extra}" if extra else ""))
    return dict(tag=tag, bw=b["win"], aw=a["win"], turn=None, kind="no divergence", note="")


def verify(root, batchdir, armjob):
    def wins(p):
        d = {}
        for ln in pathlib.Path(p).read_text().splitlines():
            f = ln.split()
            if len(f) >= 2:
                d[int(f[0])] = 9 if int(f[1]) == -1 else int(f[1])
        return d
    bad = 0
    for d in sorted(pathlib.Path(root).iterdir()):
        if not d.is_dir():
            continue
        blk, gi = d.name.rsplit("_", 1)
        gi = int(gi)
        for arm, job in (("base", "base"), ("arm", armjob)):
            ref = wins(f"{batchdir}/{job}.{blk}.wins")
            got = win_turn(json.load(open(sole(d / arm))))
            if ref.get(gi) != got:
                print(f"  REPRO MISMATCH {d.name} {arm}: batch={ref.get(gi)} repro={got}")
                bad += 1
    return bad


def main():
    root = sys.argv[1]
    if "--verify" in sys.argv:
        i = sys.argv.index("--verify")
        bad = verify(root, sys.argv[i + 1], sys.argv[i + 2])
        print(f"repro fidelity: {bad} mismatch(es)")
        if bad:
            sys.exit("refusing to classify on unfaithful repros")
    tally = {}
    rows = []
    for d in sorted(pathlib.Path(root).iterdir()):
        if not d.is_dir():
            continue
        r = classify(d.name, d / "base", d / "arm")
        if r:
            rows.append(r)
            tally[r["kind"]] = tally.get(r["kind"], 0) + 1
    for r in sorted(rows, key=lambda r: r["kind"]):
        t = f'T{r["turn"]}' if r["turn"] else "--"
        print(f'{r["tag"]:14} T{r["bw"]}->T{r["aw"]}  {t:4} {r["kind"]:38} {r["note"]}')
    print("\n=== tally ===")
    for k, v in sorted(tally.items(), key=lambda kv: -kv[1]):
        print(f"{v:3d}  {k}")


if __name__ == "__main__":
    main()
