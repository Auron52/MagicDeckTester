#!/usr/bin/env python3
"""Explain ONE changed regression game by diffing its old vs new line, turn by turn.

The audit gate (audit_changed_games.py) reports *that* a searched-depth game changed
(e.g. `gi87: 5->4`, `gi206: 4->5`); this tool shows *why*. It re-runs the single game
deterministically under the baseline binary AND the current binary, then prints an
aligned per-turn diff (lands / spells+targets / attacks / opponent life) plus:

  * whether the DRAWS DIVERGE  -> the change is a different PHYSICAL game (a fetch/shuffle
    resolved differently): variance, not a like-for-like quality change; and
  * a classification hint for a slower line (churn vs real -- run classify_turn_later.sh).

The baseline ("old") binary is the code that produced the committed ground truth. It is
saved by `regression.sh --accept` as logs/snapshots/<mode>-baseline; if that snapshot is
absent (e.g. a fresh clone) the NEW line is still shown, with instructions to obtain a
baseline. Point --old-bin at any snapshot (see test/snapshot_bin.sh) to force a diff.

Usage:
    python3 test/explain_game.py <mode> <key> <gi> [--old-bin P] [--new-bin P] [--budget-ms B]
    # e.g. python3 test/explain_game.py regression antilife_regression_d3_s2002 206

It is also imported by audit_changed_games.py, which calls diff_game(...) inline for every
searched-depth win->loss / turn-later game so the diff is in the audit output by default.
"""
import sys, os, re, json, glob, tempfile, shutil, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CASES = os.path.join(HERE, "regression_cases.sh")


# ---- resolve deck/profile/depth/seed/budget from a <deck>_<mode>_d<depth>_s<seed> key -------
def load_cases(path=CASES):
    txt = open(path).read()

    def amap(name):
        m = re.search(r"declare -A " + name + r"=\((.*?)\)", txt, re.S)
        return dict(re.findall(r"\[(\w+)\]=(\S+)", m.group(1))) if m else {}

    def arr(name):
        m = re.search(name + r"=\((.*?)\n\)", txt, re.S)
        return re.findall(r'"([^"]+)"', m.group(1)) if m else []

    return dict(
        DECK_FILE=amap("DECK_FILE"), DECK_PROF=amap("DECK_PROF"),
        cases={"smoke": arr("SMOKE_CASES"), "regression": arr("REGRESSION_CASES"),
               "overnight": arr("OVERNIGHT_CASES")})


def resolve(mode, key, cfg):
    m = re.match(r"^(.*)_(smoke|regression|overnight)_d(\d+)_s(\d+)$", key)
    if not m:
        raise ValueError(f"cannot parse key '{key}'")
    deck, kmode, depth, seed = m.group(1), m.group(2), int(m.group(3)), int(m.group(4))
    if kmode != mode:
        raise ValueError(f"key mode '{kmode}' != '{mode}'")
    budget = None
    for line in cfg["cases"].get(mode, []):
        f = line.split()
        if f[0] == deck and int(f[1]) == depth and int(f[2]) == seed:
            budget = int(f[4]); break
    if budget is None:
        raise ValueError(f"no {mode} case for {deck} d{depth} s{seed}")
    return dict(deck=deck, deckfile=cfg["DECK_FILE"][deck], profile=cfg["DECK_PROF"][deck],
                depth=depth, seed=seed, budget=budget)


# ---- run one game under a binary, parse its per-turn line ------------------------------------
def run_game(binary, r, gi):
    tmp = tempfile.mkdtemp(prefix="explain_")
    try:
        cmd = [binary, os.path.join(ROOT, r["deckfile"]),
               "--profile", os.path.join(ROOT, r["profile"]), "--games", "1",
               "--seed", str(r["seed"] + gi), "--game-index", str(gi),
               "--depth", str(r["depth"]), "--budget-ms", str(r["budget"]),
               "--log-dir", tmp]
        subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, timeout=600)
        logs = glob.glob(os.path.join(tmp, "*.json"))
        if not logs:
            return None
        return parse_log(logs[0])
    finally:
        shutil.rmtree(tmp, ignore_errors=True)


def parse_log(path):
    d = json.load(open(path))
    res = d.get("result") or {}
    win = res.get("turn") if res.get("winner") not in (None, "none") else None
    turns = {}
    for t in d.get("turns", []):
        tn = t.get("turn")
        e = turns.setdefault(tn, dict(lands=[], casts=[], attacked=False, draws=[], opp=None))
        for a in t.get("actions", []):
            ty, nm = a.get("type"), a.get("cardName", "?")
            if ty == "DRAW":
                e["draws"].append(nm)
            elif ty == "PLAY_LAND":
                e["lands"].append(nm)
            elif ty == "CAST_SPELL":
                tg = a.get("targets")
                tgt = " -> " + ",".join(x.get("cardName", "?") for x in tg) if tg else ""
                mp = a.get("manaPaid", "")
                alt = " " + mp if mp.startswith("(alt") else ""
                e["casts"].append(nm + alt + tgt)
            elif ty == "ATTACK":
                e["attacked"] = True
        ba = t.get("boardAfter") or {}
        if "opponentLife" in ba:
            e["opp"] = ba["opponentLife"]
    opening = [c.get("cardName", "?") for c in d.get("openingHand", [])]
    draws = [(tn, c) for tn in sorted(turns) for c in turns[tn]["draws"]]  # ordered post-keep draws
    return dict(win=win, turns=turns, opening=opening, draws=draws)


# ---- format one turn + diff two parsed games ------------------------------------------------
def turn_str(e):
    parts = []
    if e["lands"]:
        parts.append("land " + "/".join(e["lands"]))
    parts += e["casts"]
    if e["attacked"]:
        parts.append("ATTACK")
    s = "; ".join(parts) if parts else "(nothing)"
    if e["opp"] is not None:
        s += f"   [opp {e['opp']}]"
    return s


def wt(w):
    return "loss" if not w else f"T{w}"


def diff_game(mode, key, gi, old_bin=None, new_bin=None, budget_ms=None):
    """Return a formatted old-vs-new per-turn diff string for one game. Never raises."""
    try:
        cfg = load_cases()
        r = resolve(mode, key, cfg)
    except Exception as ex:
        return f"    [explain: cannot resolve {key} gi{gi}: {ex}]"
    if budget_ms is not None:
        r["budget"] = budget_ms
    if new_bin is None:
        new_bin = (os.path.join(ROOT, f"test/logs/{mode}/mtg.run")
                   if os.path.exists(os.path.join(ROOT, f"test/logs/{mode}/mtg.run"))
                   else os.path.join(ROOT, "build/Release/mtg"))
    if old_bin is None:
        cand = os.path.join(ROOT, f"logs/snapshots/{mode}-baseline")
        old_bin = cand if os.path.exists(cand) else None

    seedline = (f"    explain {key} gi{gi}  (seed {r['seed']}+{gi}={r['seed']+gi}, "
                f"d{r['depth']}, {r['budget']}ms)")
    new = run_game(new_bin, r, gi) if new_bin and os.path.exists(new_bin) else None
    old = run_game(old_bin, r, gi) if old_bin and os.path.exists(old_bin) else None

    out = [seedline]
    if new is None:
        return "\n".join(out + ["    [explain: could not run the current binary]"])

    if old is None:
        out.append(f"      NEW line (baseline binary unavailable -- win {wt(new['win'])}):")
        out += _one_sided(new)
        out.append("      >> for an old-vs-new diff, save a baseline once with "
                   "`regression.sh <mode> --accept` (snapshots logs/snapshots/<mode>-baseline),")
        out.append("         or pass --old-bin <snapshot> (see test/snapshot_bin.sh).")
        return "\n".join(out)

    out.append(f"      win turn:  old {wt(old['win'])}  ->  new {wt(new['win'])}")

    # Physical-game divergence, in two forms. The kept hand ignores order (a hand is a set); a
    # mid-game draw sequence is ordered (you draw one per turn). Either means the two lines are
    # not a clean like-for-like -- a decision changed which cards were kept or shuffled up.
    kept_diverge = sorted(old["opening"]) != sorted(new["opening"])
    draw_div = None
    if not kept_diverge:
        for (ot, oc), (nt, nc) in zip(old["draws"], new["draws"]):
            if oc != nc:
                draw_div = (ot, oc, nc); break
    if kept_diverge:
        out.append(f"      KEPT HANDS DIFFER -> the mulligan/bottom decision diverged (a downstream")
        out.append(f"        effect of the change); these are DIFFERENT PHYSICAL GAMES, not like-for-like.")
        out.append(f"          old kept: {', '.join(sorted(old['opening']))}")
        out.append(f"          new kept: {', '.join(sorted(new['opening']))}")
    elif draw_div is not None:
        out.append(f"      DRAWS DIVERGE from T{draw_div[0]} (old drew '{draw_div[1]}' vs new '{draw_div[2]}')")
        out.append(f"        -> a fetch/shuffle resolved differently; physically different from there on.")
    else:
        out.append("      kept hand + draws IDENTICAL -> a clean like-for-like LINE change.")

    for tn in sorted(set(old["turns"]) | set(new["turns"])):
        oe, ne = old["turns"].get(tn), new["turns"].get(tn)
        os_ = turn_str(oe) if oe else "(no turn)"
        ns_ = turn_str(ne) if ne else "(no turn)"
        if os_ == ns_:
            out.append(f"      T{tn}=  {os_}")
        else:
            out.append(f"      T{tn}   old: {os_}")
            out.append(f"      T{tn}   new: {ns_}")

    # Verdict hint. For a SLOWER win, the budget test (classify_turn_later) is authoritative --
    # always point to it -- with the divergence above as context, not a substitute.
    physically_diff = kept_diverge or draw_div is not None
    ow, nw = old["win"] or 0, new["win"] or 0
    if new["win"] and old["win"] and nw > ow:
        out.append(f"      >> SLOWER -> classify: bash test/classify_turn_later.sh {mode}")
        out.append("         recovers at higher budget = churn (benign search truncation);")
        out.append("         persists + divergence above = variance (different physical game);")
        out.append("         persists + identical draws = a REAL slowdown (inspect the first differing turn).")
    elif new["win"] and old["win"] and nw < ow:
        out.append("      >> FASTER -> improvement.")
    elif old["win"] and not new["win"]:
        tail = " -- but the divergence above shows a different physical game; confirm it is variance." \
            if physically_diff else " -- kept hand + draws identical, so a genuine capability loss."
        out.append(f"      >> WIN -> LOSS: hard-gate regression{tail}")
    elif new["win"] and not old["win"]:
        out.append("      >> LOSS -> WIN -> improvement.")
    return "\n".join(out)


def _one_sided(g):
    return [f"      T{tn}: {turn_str(g['turns'][tn])}" for tn in sorted(g["turns"])]


# ---- CLI ------------------------------------------------------------------------------------
def main():
    args = [a for a in sys.argv[1:]]
    def opt(name):
        if name in args:
            i = args.index(name); v = args[i + 1]; del args[i:i + 2]; return v
        return None
    old_bin = opt("--old-bin")
    new_bin = opt("--new-bin")
    budget = opt("--budget-ms")
    pos = [a for a in args if not a.startswith("--")]
    if len(pos) < 3:
        print(__doc__); sys.exit(2)
    mode, key, gi = pos[0], pos[1], int(pos[2])
    print(diff_game(mode, key, gi, old_bin=old_bin, new_bin=new_bin,
                    budget_ms=int(budget) if budget else None))


if __name__ == "__main__":
    main()
