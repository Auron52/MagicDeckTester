#!/usr/bin/env python3
"""The MANA COMPLETENESS ORACLE driver -- docs/design/mana-completeness-oracle.md.

USER 2026-10-26: "what I want to make sure is that the search never misses an option due to mana,
even if we largely use heuristics." The engine half (MTG_MANA_ORACLE, src/ai/ManaOracle.cpp) audits
every real main-phase decision: each menu plan is realised the way the engine pays for it AND under
every minimal payment, and the two must reach the same outcomes. This script plays the games, in ONE
pooled `mtg --batch` (never a loop of small runs), and turns the flag lines into a report.

    python3 scripts/mana_oracle.py                       # every shipping deck, 6 games each
    python3 scripts/mana_oracle.py niv fivecolour --games 20
    python3 scripts/mana_oracle.py decks/Snow --seed 9100 --name snow-check

It also plays the same batch with the oracle OFF and checks the per-job play digests match: the
oracle works on copies and must not move a single game. A mismatch is a defect in the oracle.

Output: logs/mana_oracle/<name>/report.md (+ flags.jsonl, the raw lines); scratch on build/.
Exit status: 0 = no MISS and no ORACLE-GAP; 1 = at least one; 2 = the run itself failed (an engine
error, or the oracle changed play).
"""
import argparse
import collections
import datetime
import json
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import deck_registry  # noqa: E402
from engine_bin import engine_bin  # noqa: E402

# The engine's classes (ManaOracle.cpp kClsName). MISS = the engine owes a fix (the USER's invariant);
# ORACLE-GAP = the oracle owes one; the rest are context.
MISS = ["ENUM-GAP", "APPLY-DROP", "APPLY-DROP-ORDER", "DOMINATED-CREATURE", "DOMINATED-ONESHOT",
        "DOMINATED-HASTE", "DOMINATED-LIFE", "DOMINATED-OTHER"]
ORDER = MISS + ["ORACLE-GAP", "UNMODELED", "CAPPED", "PHANTOM", "CONTINUATION-DIFFERS", "DROP-COVERED",
                "DOMINATED-LAND", "HIDDEN-INFO"]
WHAT = {
    "ENUM-GAP": "a mana gate kept this plan off the menu, yet an order and payment realise it, reaching a board no menu entry reaches",
    "DROP-COVERED": "the engine drops an action of this entry, but another entry with the same actions realises them all (no option lost)",
    "APPLY-DROP": "the menu offers the plan; the engine's payment drops an action that a legal payment pays",
    "APPLY-DROP-ORDER": "as APPLY-DROP, but the payable realisation needs a different cast order",
    "DOMINATED-CREATURE": "the engine's payment taps a creature that could attack, where another payment keeps it untapped and loses nothing",
    "DOMINATED-ONESHOT": "the engine spends a one-shot source (Treasure, Spawn) where another payment keeps it",
    "DOMINATED-HASTE": "another payment of the same plan gives a creature haste (Generator Servant's {C} spent on it)",
    "DOMINATED-LIFE": "another payment of the same plan costs less life (painlands, Ancient Tomb)",
    "DOMINATED-OTHER": "another payment keeps a resource the engine's spends (energy, counters)",
    "ORACLE-GAP": "the oracle could not reproduce the engine's own outcome -- the oracle is incomplete here",
    "UNMODELED": "the oracle cannot drive something this plan needs, so it gives no verdict",
    "CAPPED": "the payment search hit MTG_MANA_ORACLE_NODES; the complete side is partial",
    "PHANTOM": "the menu offers a plan no order or payment realises (search noise, not a miss)",
    "CONTINUATION-DIFFERS": "the engine dropped a continuation cast; the oracle's payable lines continued differently",
    "DOMINATED-LAND": "another payment leaves a land untapped -- matters only if something could use it (Stage 3)",
    "HIDDEN-INFO": "a mana gate kept this plan off the menu, and the only payments found tap a card the plan took off "
                   "the top of the library (Genesis Wave, a draw) -- clairvoyant, so no gate could know (not a miss)",
}

LINE = re.compile(r"^\[mana-oracle\] (?P<cls>[A-Z-]+) seed=(?P<seed>\d+) job=(?P<job>\S+) gi=(?P<gi>-?\d+) "
                  r"T(?P<turn>\d+) m(?P<phase>\d) plan=\{(?P<plan>.*?)\} \| (?P<detail>.*)$")
RB = re.compile(r"\|\| ROLLBACK (\S+):")
DECISION = re.compile(r"^\[mana-oracle\] DECISION seed=\d+ job=(?P<job>\S+) gi=-?\d+ T\d+ m\d plans=(?P<n>\d+)")
DIGEST = re.compile(r"^(?P<job>[^:\s]+): played=(?P<played>\d+) avg=(?P<avg>[0-9.]+) digest=(?P<digest>[0-9a-f]+)")


def suite_aliases():
    """The regression suite's short case keys (stompy, niv, th, ...) -> deck folder slug, read from
    test/regression_cases.sh's DECK_FILE map, so this tool takes the same names as the harness."""
    out = {}
    try:
        text = open(os.path.join(ROOT, "test", "regression_cases.sh")).read()
    except OSError:
        return out
    for key, path in re.findall(r'\[(\w+)\]="?(decks/[^"\n]+?\.(?:cod|txt))"?\s*\n', text):
        out.setdefault(key, deck_registry.slug(path.split("/")[1]))
    return out


def pick_decks(args, decks):
    if not args.decks:
        # Every SHIPPING list: a top-level deck folder (an archived variant is decks/<Deck>/<v1-...>).
        return [d for k, d in sorted(decks.items()) if os.path.normpath(os.path.relpath(d.dir, ROOT)).count(os.sep) == 1]
    alias = suite_aliases()
    out = []
    for name in args.decks:
        key = deck_registry.slug(os.path.basename(os.path.normpath(name)))
        key = key if key in decks else alias.get(name, key)
        if key not in decks:
            sys.exit("mana_oracle: no deck '%s' (keys: %s; suite keys: %s)"
                     % (name, ", ".join(sorted(decks)), ", ".join(sorted(alias))))
        out.append(decks[key])
    return out


def run_batch(mtg, manifest, env_extra, out_path, err_path, threads):
    env = dict(os.environ)
    env.update(env_extra)
    with open(out_path, "w") as so, open(err_path, "w") as se:
        rc = subprocess.run([mtg, "--batch", manifest, "--threads", str(threads)], stdout=so, stderr=se,
                            env=env, cwd=ROOT).returncode
    return rc


def digests(path):
    out = {}
    for ln in open(path):
        m = DIGEST.match(ln.strip())
        if m:
            out[m.group("job")] = (m.group("played"), m.group("avg"), m.group("digest"))
    return out


def norm(s):
    """Fold the parts of a flag that vary between otherwise identical defects (card ids, X, counts)."""
    s = re.sub(r"#\d+", "", s)
    s = re.sub(r"\(x\d+\)", "(xN)", s)
    s = re.sub(r"\bx\d+\b", "xN", s)
    s = re.sub(r"\d+ oracle outcomes", "N oracle outcomes", s)
    s = re.sub(r"[+-]\d+ life", "+N life", s)
    return s


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("decks", nargs="*", help="deck keys / folders (default: every shipping deck)")
    ap.add_argument("--games", type=int, default=6)
    ap.add_argument("--seed", type=int, default=424200)
    ap.add_argument("--game-index", type=int, default=0,
                    help="first game index: game i is (seed + i, game index + i). The play viewer's games are "
                         "seed = gi + 1, so viewer seeds A..B are --seed A --game-index A-1 --games B-A+1")
    ap.add_argument("--threads", type=int, default=os.cpu_count() or 8)
    ap.add_argument("--nodes", type=int, default=0, help="MTG_MANA_ORACLE_NODES (per-plan apply cap; 0 = engine default)")
    ap.add_argument("--name", default=None, help="run name (default: a timestamp)")
    ap.add_argument("--bin", default=None, help="engine binary (default: engine_bin)")
    ap.add_argument("--no-identity", action="store_true", help="skip the oracle-OFF arm (no play-identity check)")
    ap.add_argument("--top", type=int, default=12, help="signatures listed per class in the report")
    args = ap.parse_args()

    decks = deck_registry.discover(os.path.join(ROOT, "decks"))
    decks = {k: d for k, d in decks.items()}
    chosen = pick_decks(args, decks)
    name = args.name or datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out_dir = os.path.join(ROOT, "logs", "mana_oracle", name)
    scratch = os.path.join(ROOT, "build", "scratch", "mana_oracle", name)
    os.makedirs(out_dir, exist_ok=True)
    os.makedirs(scratch, exist_ok=True)
    mtg = args.bin or engine_bin("mtg")

    jobs = []
    for d in chosen:
        j = {"name": d.key, "deck": os.path.relpath(d.deck_file, ROOT), "games": args.games, "seed": args.seed}
        if args.game_index:
            j["game_index"] = args.game_index
        if os.path.exists(d.profile):
            j["profile"] = os.path.relpath(d.profile, ROOT)
        jobs.append(j)
    manifest = os.path.join(scratch, "manifest.json")
    json.dump({"jobs": jobs}, open(manifest, "w"), indent=1)
    deck_of = {j["name"]: j for j in jobs}

    env_on = {"MTG_MANA_ORACLE": "2"}
    if args.nodes > 0:
        env_on["MTG_MANA_ORACLE_NODES"] = str(args.nodes)
    print("mana_oracle: %d decks x %d games, seed %d, game index %d, %s"
          % (len(jobs), args.games, args.seed, args.game_index, mtg), file=sys.stderr)
    rc_on = run_batch(mtg, manifest, env_on, os.path.join(scratch, "on.out"), os.path.join(scratch, "on.err"),
                      args.threads)
    identity = None
    if not args.no_identity:
        rc_off = run_batch(mtg, manifest, {"MTG_MANA_ORACLE": "0"}, os.path.join(scratch, "off.out"),
                           os.path.join(scratch, "off.err"), args.threads)
        d_on, d_off = digests(os.path.join(scratch, "on.out")), digests(os.path.join(scratch, "off.out"))
        identity = (rc_off == 0 and d_on == d_off and len(d_on) == len(jobs))
        if not identity:
            bad = sorted(k for k in set(d_on) | set(d_off) if d_on.get(k) != d_off.get(k))
            print("mana_oracle: PLAY MOVED with the oracle on (or a job is missing): %s" % ", ".join(bad), file=sys.stderr)

    flags, decisions, plans_seen, summary = [], collections.Counter(), collections.Counter(), []
    with open(os.path.join(scratch, "on.err"), errors="replace") as fh, \
         open(os.path.join(out_dir, "flags.jsonl"), "w") as fj:
        for ln in fh:
            ln = ln.rstrip("\n")
            if ln.startswith("=== MANA ORACLE"):
                summary.append(ln)
                continue
            m = DECISION.match(ln)
            if m:
                decisions[m.group("job")] += 1
                plans_seen[m.group("job")] += int(m.group("n"))
                continue
            m = LINE.match(ln)
            if not m or m.group("cls") == "DECISION":
                continue
            rec = m.groupdict()
            # The engine prints the job-LOCAL game number; the viewer's game is the global one.
            j0 = deck_of.get(rec["job"])
            rec["gi"] = str(int(rec["gi"]) + (j0.get("game_index", 0) if j0 else 0))
            detail, _, board = rec["detail"].rpartition(" | ")
            rec["detail"], rec["board"] = (detail, board) if detail else (rec["detail"], "")
            flags.append(rec)
            fj.write(json.dumps(rec) + "\n")

    by_cls = collections.defaultdict(lambda: collections.Counter())
    sigs = collections.defaultdict(dict)   # cls -> sig -> {count, example}
    for f in flags:
        by_cls[f["cls"]][f["job"]] += 1
        detail = f["detail"].split("; the engine ended")[0].split("; engine ended")[0]
        sig = (f["job"], norm(f["plan"]), norm(detail)[:240])
        e = sigs[f["cls"]].setdefault(sig, {"count": 0, "ex": f})
        e["count"] += 1

    def repro(f):
        j = deck_of.get(f["job"])
        if j is None:
            return ""
        prof = (" --profile '%s'" % j["profile"]) if "profile" in j else ""
        return ("MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=%s build/Release/mtg '%s'%s --games 1 --seed %s --game-index %s"
                % (f["turn"], j["deck"], prof, f["seed"], f["gi"]))

    miss_total = sum(sum(by_cls[c].values()) for c in MISS)
    gap_total = sum(by_cls["ORACLE-GAP"].values())
    rep = []
    rep.append("# Mana oracle run `%s`\n" % name)
    rep.append("%d decks x %d games, seed %d%s, binary `%s`. Engine: `MTG_MANA_ORACLE=2`%s.\n"
               % (len(jobs), args.games, args.seed,
                  (", game index %d" % args.game_index) if args.game_index else "", os.path.relpath(mtg, ROOT),
                  (", `MTG_MANA_ORACLE_NODES=%d`" % args.nodes) if args.nodes else ""))
    rep.append("**MISS = %d, ORACLE-GAP = %d.** Play identity with the oracle off: %s.\n"
               % (miss_total, gap_total,
                  "not checked" if identity is None else ("byte-identical" if identity else "**MOVED -- the run is invalid**")))
    if rc_on != 0:
        rep.append("**The oracle-on batch exited %d.**\n" % rc_on)
    rep.append("```\n" + "\n".join(summary) + "\n```\n")
    # Per-deck table.
    cols = [c for c in ORDER if sum(by_cls[c].values())]
    rep.append("## Per deck\n")
    rep.append("| deck | decisions | plans | " + " | ".join(cols) + " |")
    rep.append("|---|---|---|" + "---|" * len(cols))
    for j in jobs:
        k = j["name"]
        rep.append("| %s | %d | %d | %s |" % (k, decisions[k], plans_seen[k],
                                            " | ".join(str(by_cls[c][k]) for c in cols)))
    rep.append("")
    # The rollback census (USER 2026-10-27): per lost line, how far back the engine's mana decisions have to
    # be re-made for the line to pay (APPLY-DROP flags carry the verdict), plus the reorder / off-menu lines.
    rb = collections.defaultdict(collections.Counter)
    for f in flags:
        if f["cls"] == "APPLY-DROP":
            m = RB.search(f["detail"])
            v = m.group(1) if m else "no-census"
            rb[f["job"]][v] += 1
            if "MTG_PAY_ROLLBACK (forced on) recovers it" in f["detail"]:
                rb[f["job"]]["rescued"] += 1
            if "was not declared in the plan" in f["detail"]:
                rb[f["job"]]["need-later"] += 1
        elif f["cls"] == "APPLY-DROP-ORDER":
            rb[f["job"]]["reorder"] += 1
        elif f["cls"] == "ENUM-GAP":
            rb[f["job"]]["off-menu"] += 1
    if rb:
        rcols = ["depth0", "depth1", "depth2", "depth3+", "capped", "unfaithful", "no-failed-payment",
                 "unmodeled", "inconsistent", "no-census", "reorder", "off-menu", "rescued", "need-later"]
        rcols = [c for c in rcols if any(rb[k][c] for k in rb)]
        rep.append("## Rollback census\n")
        rep.append("_Lost lines: APPLY-DROP split by DEPTH -- how many of the engine's mana decisions before the "
                   "failing payment must be re-made (0 = the failing payment itself had a way; 1 = the payment before "
                   "it, i.e. a one-step rollback); reorder = only another cast order pays (APPLY-DROP-ORDER); off-menu "
                   "= a mana gate kept it off the menu (ENUM-GAP). rescued = MTG_PAY_ROLLBACK forced on recovers the "
                   "APPLY-DROP; need-later = a lost action the plan did not declare; unmodeled = every paying line rests "
                   "on an engine mana choice the oracle does not drive (no depth)._\n")
        rep.append("| deck | " + " | ".join(rcols) + " |")
        rep.append("|---|" + "---|" * len(rcols))
        for j in jobs:
            k = j["name"]
            if k in rb:
                rep.append("| %s | %s |" % (k, " | ".join(str(rb[k][c]) for c in rcols)))
        rep.append("")
    for c in ORDER:
        if not sigs.get(c):
            continue
        items = sorted(sigs[c].items(), key=lambda kv: -kv[1]["count"])
        rep.append("## %s (%d flags, %d signatures)\n" % (c, sum(by_cls[c].values()), len(items)))
        rep.append("_%s._\n" % WHAT.get(c, ""))
        for (job, plan, detail), e in items[:args.top]:
            f = e["ex"]
            rep.append("- **%s** x%d -- T%s m%s `{%s}`  \n  %s  \n  board: %s  \n  repro: `%s`"
                       % (job, e["count"], f["turn"], f["phase"], f["plan"], f["detail"][:700], f["board"][:400],
                          repro(f)))
        if len(items) > args.top:
            rep.append("- ... %d more signatures in flags.jsonl" % (len(items) - args.top))
        rep.append("")
    open(os.path.join(out_dir, "report.md"), "w").write("\n".join(rep) + "\n")
    print("mana_oracle: MISS=%d ORACLE-GAP=%d identity=%s -> %s" % (miss_total, gap_total, identity,
          os.path.relpath(os.path.join(out_dir, "report.md"), ROOT)), file=sys.stderr)
    if rc_on != 0 or identity is False:
        return 2
    return 1 if (miss_total or gap_total) else 0


if __name__ == "__main__":
    sys.exit(main())
