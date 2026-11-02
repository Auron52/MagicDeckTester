#!/usr/bin/env python3
"""Reconstruct Cyclone pruning experiments from the tracked branch ledger.

Writes manifests only; never launches an engine. All arms in each stage share a
single manifest/work queue. The process-only T1 order diagnostic requires running
the SAME iso92 manifest twice, once normally and once with its env override.

Examples:
  python3 reconstruct_prunes.py --worktree /path/to/cyclone-finish --output /path/to/scratch phasec
  python3 reconstruct_prunes.py --worktree /path/to/cyclone-finish --output /path/to/scratch iso92
  python3 reconstruct_prunes.py --worktree /path/to/cyclone-finish --output /path/to/scratch report phasec
  python3 reconstruct_prunes.py --worktree /path/to/cyclone-finish --output /path/to/scratch blind

Blind's default is the symmetric union of outcome-changed games for each arm,
not an unbiased estimate of the effect over the complete deck distribution. Use
--all-games when the intended question is the full-sample blind effect.
"""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shlex
import statistics
import subprocess
import sys


CELLS = {3: (109000, 400, 10), 5: (110000, 200, 20)}
OFF = (
    "PB_PLANS", "PONDER_PLANS", "SEED_PLANS", "CYCLER_RULE",
    "PB_PLAIN_RANK", "CANTRIP_SITUATIONAL", "KEEP_AMEND", "KEEP_MYRIAD",
    "PUTBACK_SINGLE", "PUTBACK_RULE", "PONDER_KEEPSHUFFLE", "PUTBACK_USABLE",
)
OTHER_OFF = (
    "MTG_CYCLONE_CAST_ORDER", "MTG_CYCLONE_ORDER_DRAW_TURNS",
    "MTG_CYCLONE_CYCLE_ORDER", "MTG_CYCLONE_CYCLE_CHEAP",
    "MTG_CYCLONE_CO_REV_CYCLERS", "MTG_CYCLONE_CO_CYCLERS_BEFORE_SONG",
    "MTG_CYCLONE_CO_REV_ELDRAZI", "MTG_CYCLONE_CO_REV_SONG",
    "MTG_CYCLONE_CO_REV_FLAT", "MTG_CYCLONE_CO_REV_TOTAL",
    "MTG_CYCLE_SEED_PAY_SPLIT", "MTG_LEAF_KILL_ONLY_NOM2", "MTG_NESTED_EXH_AXIS",
    "MTG_CYCLONE_DISCARD_DIG_PUTBACK_FIRST", "MTG_CYCLONE_DISCARD_SHUFFLE_PLAN",
    "MTG_CYCLONE_BUCKET_KEEPNEED", "MTG_CYCLONE_DISCARD_ELDRAZI2_SHUFFLE_EARLY",
)
ARMS = {
    "base": (),
    "pb": ("PB_PLANS",),
    "cycler": ("CYCLER_RULE",),
    "pbcyc": ("PB_PLANS", "CYCLER_RULE"),
    "pba": ("PB_PLANS", "KEEP_AMEND", "KEEP_MYRIAD"),
    "pbanomyr": ("PB_PLANS", "KEEP_AMEND"),
    "pbcyca": ("PB_PLANS", "CYCLER_RULE", "KEEP_AMEND", "KEEP_MYRIAD"),
    "pbcycanomyr": ("PB_PLANS", "CYCLER_RULE", "KEEP_AMEND"),
    "situ": ("CANTRIP_SITUATIONAL",),
    "seed": ("SEED_PLANS",),
    "ponder": ("PONDER_PLANS",),
    "pbcycponder": ("PB_PLANS", "CYCLER_RULE", "PONDER_PLANS"),
}
DEFAULT_ARMS = "base,pb,cycler,pbcyc,pba,pbanomyr,pbcyca,pbcycanomyr,situ,seed,ponder,pbcycponder"


def flag(name):
    return "MTG_CYCLONE_" + name


def git(worktree, *args):
    return subprocess.check_output(["git", "-C", str(worktree), *args], text=True,
                                   env={**os.environ, "GIT_OPTIONAL_LOCKS": "0"}).strip()


def dump(path, data):
    """Never replace a different manifest beneath an existing measurement."""
    text = json.dumps(data, indent=2) + "\n"
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists() and path.read_text() != text:
        raise ValueError(f"refusing to replace different existing file: {path}; use a new output directory")
    path.write_text(text)


def check_schema(worktree, need_blind=False):
    runner = (worktree / "src/runner/BatchRunner.cpp").read_text()
    slots = (worktree / "src/ai/HeuristicArm.h").read_text()
    if 'jspec.contains("flags")' not in runner:
        raise ValueError("BatchRunner has no per-job flags object support")
    if need_blind and 'jspec.value("blind_salt"' not in runner:
        raise ValueError("BatchRunner lacks per-job blind_salt; integrate the pruning branch first")
    required = [flag(x) for x in OFF] + [flag("CANTRIP_ORDER")] + list(OTHER_OFF)
    absent = [x for x in required if f'"{x}"' not in slots]
    if absent:
        raise ValueError(f"HeuristicArm has no slot for {absent}; integrate the pruning branch first")


def pins(arm):
    out = {flag(x): False for x in OFF}
    out.update({x: False for x in OTHER_OFF})
    out[flag("CANTRIP_ORDER")] = True
    out.update(MTG_LEAF_KILL_ONLY_EXTRA=True,
               MTG_CYCLONE_BUCKET_DISCARD=True,
               MTG_CYCLONE_DISCARD_ELDRAZI2_OVER_REMAND=True,
               MTG_CYCLONE_BUCKET_PUTBACK_TIEBREAK=True)
    out.update({flag(x): True for x in ARMS[arm]})
    return out


def job(worktree, arm, depth, games=None, gi=0, salt=None):
    base, count, budget = CELLS[depth]
    name = f"{arm}_d{depth}"
    if salt is not None:
        name += f"_gi{gi}_bs{salt}"
    deck = worktree / "decks/Unpredictable Cyclone/Unpredictable Cyclone.cod"
    item = {
        "name": name,
        "deck": str(deck),
        "profile": str(deck.with_suffix(".profile.json")),
        "games": count if games is None else games,
        "seed": base + gi,
        "game_index": gi,
        "depth": depth,
        "budget_ms": budget,
        "ignore_play_profile": depth == 3,
        "max_turns": 8,
        "flags": pins(arm),
    }
    if salt is not None:
        item["blind_salt"] = salt
    return item


def phase_chunks(worktree, arms, chunk):
    jobs = []
    # Equal weight per chunk offset interleaves arms/depths within ONE queue.
    # This is not a sequence of separate batch invocations or scheduling barriers.
    for gi in range(0, max(x[1] for x in CELLS.values()), chunk):
        for depth, (_, count, _) in CELLS.items():
            if gi >= count:
                continue
            for arm in arms:
                item = job(worktree, arm, depth, games=min(chunk, count - gi), gi=gi)
                item["name"] = f"phasec_{arm}_d{depth}_off{gi}"
                item["weight"] = 100000 - gi // chunk
                jobs.append(item)
    return jobs


def phase_arm(job):
    return job["name"].removeprefix("phasec_").split("_d", 1)[0]


def primary_control(arm, arms):
    # CYCLER_RULE already ships ON: PBCYC-vs-CYCLER is the PB adoption
    # contrast. Historical all-off/PB comparisons remain useful attribution.
    if arm == "pbcyc" and "cycler" in arms:
        return "cycler"
    if arm.startswith("pbcyc") and arm != "pbcyc" and "pbcyc" in arms:
        return "pbcyc"
    if arm.startswith("pb") and arm != "pb" and "pb" in arms:
        return "pb"
    return "base"


def provenance(worktree):
    unmerged = git(worktree, "diff", "--name-only", "--diff-filter=U")
    if unmerged:
        raise ValueError(f"source integration has unresolved conflicts; finish it before stamping manifests: {unmerged}")
    profile = worktree / "decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json"
    source = hashlib.sha256()
    for name in sorted(git(worktree, "ls-files", "--", "src").splitlines()):
        data = (worktree / name).read_bytes()
        source.update(name.encode() + b"\0" + str(len(data)).encode() + b"\0" + data)
    return {
        "worktree": str(worktree),
        "commit": git(worktree, "rev-parse", "HEAD"),
        "src_tree": git(worktree, "rev-parse", "HEAD:src"),
        "src_status": git(worktree, "status", "--short", "--", "src"),
        "src_patch_sha256": hashlib.sha256(git(worktree, "diff", "--binary", "HEAD", "--", "src").encode()).hexdigest(),
        "src_worktree_sha256": source.hexdigest(),
        "profile_sha256": hashlib.sha256(profile.read_bytes()).hexdigest(),
        "source": "origin/cyclone-prunes:docs/design/analysis-Unpredictable Cyclone.md (Phase C and QP1-QP8)",
        "notes": [
            "Reconstructed inputs; unavailable original scratch manifests are not claimed byte-identical.",
            "Every baseline is recomputed on this source tree with explicit pruning pins.",
            "Searched bottoming is derived from depth; no bottom-rollout override is applied.",
            "All games are scored at their win round, with unwon games scored 9.",
            "KEEP_AMEND requires the library-density rank-cache-key fix before measurement.",
        ],
    }


def write_stage(worktree, output, stage, jobs, details, print_run=True, binary=None, threads=12):
    target = output / stage
    dump(target / "manifest.json", {"jobs": jobs})
    dump(target / "meta.json", {**provenance(worktree), **details})
    q = shlex.quote
    binary = binary or worktree / "build/PGO/mtg"
    argv = [str(binary), "--batch", str(target / "manifest.json"),
            "--cards-json", str(worktree / "src/cards/data/cards.json"),
            "--threads", str(threads), "--game-log-dir", str(target / "wins"),
            "--game-trace-dir", str(target / "traces")]
    environment = {"MTG_DUMP_UNITS": "1", "MTG_SLOW_GAME_MS": "1"}
    dump(target / "run-plan.json", {"cwd": str(worktree), "argv": argv, "env": environment,
                                   "stdout": str(target / "batch.log"), "stderr": str(target / "batch.err"),
                                   "requires": "completed source freeze, fresh PGO, no other generation running"})
    cmd = (f"env MTG_DUMP_UNITS=1 MTG_SLOW_GAME_MS=1 "
           f"{q(str(binary))} --batch {q(str(target / 'manifest.json'))} "
           f"--cards-json {q(str(worktree / 'src/cards/data/cards.json'))} "
           f"--threads {threads} --game-log-dir {q(str(target / 'wins'))} "
           f"--game-trace-dir {q(str(target / 'traces'))} "
           f"> {q(str(target / 'batch.log'))} 2> {q(str(target / 'batch.err'))}")
    print(f"{target / 'manifest.json'}: {len(jobs)} jobs, {sum(j['games'] for j in jobs)} games")
    if print_run:
        print("Run from worktree after its source freeze and fresh PGO build:")
        print(cmd)
    return target


def parse_arms(text):
    arms = text.split(",")
    if len(set(arms)) != len(arms) or any(x not in ARMS for x in arms):
        raise ValueError(f"arms must be distinct names in {list(ARMS)}")
    if "base" not in arms:
        arms.insert(0, "base")
    return arms


def read_wins(path):
    if not path.exists():
        raise ValueError(f"missing completed output: {path}")
    out = {}
    for line in path.read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        p = line.split()
        gi = int(p[0])
        if gi in out:
            raise ValueError(f"duplicate game {gi}: {path}")
        out[gi] = (int(p[1]), p[2] if len(p) > 2 else None)
    return out


def score(result):
    return result[0] if result[0] > 0 else 9


def paired(a, b):
    if a.keys() != b.keys():
        raise ValueError(f"unpaired games: base-only={sorted(a.keys()-b.keys())}, arm-only={sorted(b.keys()-a.keys())}")
    return {gi: score(b[gi]) - score(a[gi]) for gi in sorted(a)}


def brief(values):
    values = list(values)
    n = len(values)
    mean = statistics.mean(values) if n else None
    return {
        "n": n,
        "delta": mean,
        "se": statistics.stdev(values) / math.sqrt(n) if n > 1 else None,
        "slower": sum(x > 0 for x in values),
        "faster": sum(x < 0 for x in values),
        "tied": sum(x == 0 for x in values),
    }


def phase_report(output):
    stage = output / "phasec"
    manifest = json.loads((stage / "manifest.json").read_text())
    results = {}
    arms = list(dict.fromkeys(phase_arm(j) for j in manifest["jobs"]))
    timings = {}
    if (stage / "batch.log").exists():
        for line in (stage / "batch.log").read_text().splitlines():
            match = re.match(r"^(\S+):.*\bms=(\d+)\b.*\bunits=(\d+)\b", line)
            if match:
                timings[match[1]] = int(match[2])
    for depth, (_, count, _) in CELLS.items():
        samples = {}
        units, thread_ms = {}, {}
        for arm in arms:
            samples[arm] = {}
            arm_units, arm_ms = 0, 0
            have_units, have_ms = True, True
            for j in manifest["jobs"]:
                if phase_arm(j) != arm or j["depth"] != depth:
                    continue
                part = read_wins(stage / "wins" / (j["name"] + ".wins"))
                if set(part) != set(range(j["game_index"], j["game_index"] + j["games"])):
                    raise ValueError(f"incomplete chunk {j['name']}")
                if samples[arm].keys() & part.keys():
                    raise ValueError(f"overlapping chunks for {arm} d{depth}")
                samples[arm].update(part)
                unit_file = stage / "wins" / (j["name"] + ".units")
                if unit_file.exists():
                    measured = {int(p[0]): int(p[1]) for line in unit_file.read_text().splitlines() if (p := line.split())}
                    if measured.keys() != part.keys():
                        raise ValueError(f"unpaired work-unit output: {unit_file}")
                    arm_units += sum(measured.values())
                else:
                    have_units = False
                if j["name"] in timings:
                    arm_ms += timings[j["name"]]
                else:
                    have_ms = False
            if set(samples[arm]) != set(range(count)):
                raise ValueError(f"incomplete {arm} d{depth}: expected games 0..{count-1}")
            units[arm] = arm_units if have_units else None
            thread_ms[arm] = arm_ms if have_ms else None
        for arm in arms:
            if arm == "base":
                continue
            controls = ["base"]
            if arm.startswith("pbcyc") and arm != "pbcyc" and "pbcyc" in arms:
                controls.append("pbcyc")
            elif arm.startswith("pb") and arm != "pb" and "pb" in arms:
                controls.append("pb")
            live_control = primary_control(arm, arms)
            if live_control not in controls:
                controls.append(live_control)
            # Isolate MYRIAD from the shared KEEP_AMEND change with existing
            # paired data; retain each combined amendment's PB/PBCYC rows too.
            myriad_control = {"pba": "pbanomyr", "pbcyca": "pbcycanomyr"}.get(arm)
            if myriad_control in arms and myriad_control not in controls:
                controls.append(myriad_control)
            for control in controls:
                deltas = paired(samples[control], samples[arm])
                key = f"{arm}-vs-{control}_d{depth}"
                results[key] = {
                    **brief(deltas.values()),
                    "primary_comparison": control == live_control,
                    "baseline_avg": statistics.mean(score(v) for v in samples[control].values()),
                    "arm_avg": statistics.mean(score(v) for v in samples[arm].values()),
                    "units_ratio": units[arm] / units[control] if units[arm] is not None and units[control] else None,
                    "thread_ms_ratio": thread_ms[arm] / thread_ms[control] if thread_ms[arm] is not None and thread_ms[control] else None,
                    "slower_gi": [gi for gi, d in deltas.items() if d > 0],
                    "faster_gi": [gi for gi, d in deltas.items() if d < 0],
                    "same_score_play_changed_gi": [gi for gi, d in deltas.items() if d == 0 and samples[control][gi][1] != samples[arm][gi][1]],
                }
    dump(stage / "paired_report.json", results)
    print(json.dumps(results, indent=2))
    return arms, results


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--worktree", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--bin", type=Path, help="one fresh PGO binary for all arms; defaults to worktree/build/PGO/mtg")
    parser.add_argument("--threads", type=int, default=12)
    subs = parser.add_subparsers(dest="command", required=True)
    phase = subs.add_parser("phasec")
    phase.add_argument("--arms", default=DEFAULT_ARMS)
    phase.add_argument("--chunk", type=int, default=10)
    subs.add_parser("iso92")
    blind = subs.add_parser("blind")
    blind.add_argument("--salts", type=int, default=4)
    blind.add_argument("--all-games", action="store_true")
    report = subs.add_parser("report")
    report.add_argument("stage", choices=("phasec", "blind", "iso92"))
    args = parser.parse_args()
    worktree, output = args.worktree.resolve(), args.output.resolve()
    binary = args.bin.resolve() if args.bin else worktree / "build/PGO/mtg"
    if args.threads < 1:
        raise ValueError("--threads must be positive")
    if args.command == "phasec":
        check_schema(worktree)
        arms = parse_arms(args.arms)
        if args.chunk < 1:
            raise ValueError("--chunk must be positive")
        jobs = phase_chunks(worktree, arms, args.chunk)
        logical = {j["name"]: {"arm": phase_arm(j), "depth": j["depth"],
                   "logical_cell": f"phasec_{phase_arm(j)}_d{j['depth']}",
                   "base_seed": CELLS[j["depth"]][0], "first_gi": j["game_index"]} for j in jobs}
        write_stage(worktree, output, "phasec", jobs, {"arms": arms, "job_map": logical}, binary=binary, threads=args.threads)
    elif args.command == "iso92":
        check_schema(worktree, need_blind=True)
        jobs = [job(worktree, "pb", 3, games=1, gi=92, salt=s) for s in range(1, 41)]
        target = write_stage(worktree, output, "iso92", jobs, {
            "salts": list(range(1, 41)),
            "normal": "fixed JUNK: Gyre first, Emrakul second",
            "reversed": "MTG_DIAG_PB_T1_ORDER=2: old Emrakul-first order, turn 1 only",
        }, print_run=False, binary=binary, threads=args.threads)
        print("Run this one manifest twice into distinct output directories:")
        for name, diagnostic in (("normal", 0), ("reversed", 2)):
            directory = target / name
            directory.mkdir(parents=True, exist_ok=True)
            q = shlex.quote
            print(f"env MTG_DUMP_UNITS=1 MTG_SLOW_GAME_MS=1 MTG_DIAG_PB_T1_ORDER={diagnostic} "
                  f"{q(str(binary))} --batch {q(str(target / 'manifest.json'))} "
                  f"--cards-json {q(str(worktree / 'src/cards/data/cards.json'))} "
                  f"--threads {args.threads} --game-log-dir {q(str(directory / 'wins'))} "
                  f"--game-trace-dir {q(str(directory / 'traces'))} "
                  f"> {q(str(directory / 'batch.log'))} 2> {q(str(directory / 'batch.err'))}")
        print("The diagnostic is process-only, not a per-job heurarm flag. Trace-filter actually forced T1 Brainstorms separately.")
    elif args.command == "blind":
        check_schema(worktree, need_blind=True)
        if args.salts <= 0:
            raise ValueError("--salts must be positive")
        earlier = json.loads((output / "phasec/meta.json").read_text())
        now = provenance(worktree)
        for field in ("src_worktree_sha256", "profile_sha256"):
            if earlier.get(field) != now[field]:
                raise ValueError(f"Phase C source/apparatus changed ({field}); recompute Phase C controls before the blind ensemble")
        arms, results = phase_report(output)
        jobs, comparisons = [], []
        base_seen = set()
        for depth, (_, count, _) in CELLS.items():
            for arm in arms:
                if arm == "base":
                    continue
                # Include amendment-vs-PB comparisons as well as every arm-vs-base.
                for key, result in results.items():
                    if not key.startswith(arm + "-vs-") or not key.endswith(f"_d{depth}"):
                        continue
                    control = key.split("-vs-", 1)[1].rsplit("_d", 1)[0]
                    # Use the LIVE control for admission decisions. In particular
                    # PB adoption is PBCYC-vs-CYCLER, because CYCLER already ships.
                    # Keep historical comparisons in raw reporting only.
                    if control != primary_control(arm, arms):
                        continue
                    indices = list(range(count)) if args.all_games else sorted(result["slower_gi"] + result["faster_gi"])
                    for gi in indices:
                        for salt in range(1, args.salts + 1):
                            for which in (control, arm):
                                identity = which, depth, gi, salt
                                if identity not in base_seen:
                                    jobs.append(job(worktree, which, depth, games=1, gi=gi, salt=salt))
                                    base_seen.add(identity)
                    comparisons.append({"arm": arm, "control": control, "depth": depth, "indices": indices})
        write_stage(worktree, output, "blind", jobs, {
            "salts": list(range(1, args.salts + 1)), "all_games": args.all_games,
            "comparisons": comparisons,
            "interpretation": "full Phase C sample" if args.all_games else "conditional on clairvoyant outcome-changed games; not the deck-wide effect",
            "limitations": "No real-resolution chooser for seed Curator scry or Cyclone take/decline pins, same limitation in both arms.",
        }, binary=binary, threads=args.threads)
    elif args.stage == "phasec":
        phase_report(output)
    elif args.stage == "iso92":
        from prunes_proofs import iso92_report
        result = iso92_report(output / "iso92")
        dump(output / "iso92/report.json", result)
        print(json.dumps({k: v for k, v in result.items() if k != "rows"}, indent=2))
    else:
        meta = json.loads((output / "blind/meta.json").read_text())
        results = {}
        for comparison in meta["comparisons"]:
            arm, control, depth = comparison["arm"], comparison["control"], comparison["depth"]
            game_means, details = {}, {}
            for gi in comparison["indices"]:
                deltas = []
                for salt in meta["salts"]:
                    stem = f"_d{depth}_gi{gi}_bs{salt}.wins"
                    a = read_wins(output / "blind/wins" / (control + stem))
                    b = read_wins(output / "blind/wins" / (arm + stem))
                    if set(a) != {gi} or set(b) != {gi}:
                        raise ValueError(f"incomplete {arm} vs {control} d{depth} gi{gi} salt{salt}")
                    deltas.append(paired(a, b)[gi])
                game_means[gi] = statistics.mean(deltas)
                details[str(gi)] = brief(deltas)
            results[f"{arm}-vs-{control}_d{depth}"] = {
                **brief(game_means.values()), "per_game": details,
                "se_unit": "game: salts are averaged within each real game before calculating aggregate SE",
                "interpretation": meta["interpretation"],
            }
        dump(output / "blind/report.json", results)
        print(json.dumps(results, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(2)
