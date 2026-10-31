#!/usr/bin/env python3
"""Reconstruct Cyclone's documented experiments; emit inputs, never launch work.

Examples:
  python3 make_validation_manifests.py --worktree /path/to/cyclone-finish \
      --bin /path/to/build/Release/mtg --out /path/to/build/scratch/cyclone-finish/validation \
      --experiment cost --experiment memory

Run plans use absolute paths and keep every independent experiment pooled. Memory
variants reuse ONE manifest in separate processes because quota flags are static.
Historical A/B baselines explicitly pin the relevant proposal flags OFF; a final
cost gate instead measures the LIVE compiled defaults, unless --cost-flags supplies
the exact pending configuration. The latter is labelled a what-if, never live.
"""
import argparse
import copy
import hashlib
import json
import math
import os
import re
import shlex
import subprocess
from pathlib import Path


PROPOSALS = [
    "CYCLONE_PB_PLANS", "CYCLONE_PONDER_PLANS", "CYCLONE_SEED_PLANS",
    "CYCLONE_CYCLER_RULE", "CYCLONE_PB_PLAIN_RANK", "CYCLONE_CANTRIP_SITUATIONAL",
    "CYCLONE_KEEP_AMEND", "CYCLONE_KEEP_MYRIAD", "CYCLONE_CAST_ORDER",
    "CYCLONE_ORDER_DRAW_TURNS", "CYCLONE_CYCLE_ORDER", "CYCLONE_CYCLE_CHEAP",
    "CYCLE_SEED_PAY_SPLIT", "CYCLONE_CO_REV_CYCLERS", "CYCLONE_CO_CYCLERS_BEFORE_SONG", "CYCLONE_CO_REV_ELDRAZI",
    "CYCLONE_CO_REV_SONG", "CYCLONE_CO_REV_FLAT", "CYCLONE_CO_REV_TOTAL",
    "LEAF_KILL_ONLY_NOM2", "NESTED_EXH_AXIS",
]
BASE = {"MTG_" + key: False for key in PROPOSALS}
BASE.update(MTG_CYCLONE_CANTRIP_ORDER=True,
            MTG_CYCLONE_PUTBACK_RULE=False, MTG_CYCLONE_PUTBACK_SINGLE=False,
            MTG_CYCLONE_PONDER_KEEPSHUFFLE=False, MTG_CYCLONE_PUTBACK_USABLE=False,
            MTG_LEAF_KILL_ONLY_EXTRA=True,
            MTG_CYCLONE_BUCKET_DISCARD=True,
            MTG_CYCLONE_DISCARD_ELDRAZI2_OVER_REMAND=True,
            MTG_CYCLONE_BUCKET_PUTBACK_TIEBREAK=True,
            MTG_CYCLONE_DISCARD_DIG_PUTBACK_FIRST=False,
            MTG_CYCLONE_DISCARD_SHUFFLE_PLAN=False,
            MTG_CYCLONE_BUCKET_KEEPNEED=False,
            MTG_CYCLONE_DISCARD_ELDRAZI2_SHUFFLE_EARLY=False)

PROCESS_CACHE_OVERRIDES = {
    "MTG_FSL_POOL", "MTG_FSL_CAP", "MTG_MEM_BUDGET_MB",
    "MTG_PLAN_CACHE_EXACT_KEY", "MTG_BP_DERIVE_ISOLATE", "MTG_CACHE_HIT_CHARGE",
}


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args], text=True,
                                   env={**os.environ, "GIT_OPTIONAL_LOCKS": "0"}).strip()


def flags(**on):
    return {**BASE, **{"MTG_" + k: v for k, v in on.items()}}


def read_suite(root):
    text = (root / "test/regression_cases.sh").read_text()
    def mapping(name):
        block = re.search(r"declare -A " + name + r"=\((.*?)\n\)", text, re.S).group(1)
        return {k: v.strip('"') for k, v in re.findall(r'\[(\w+)\]=("[^"]*"|\S+)', block)}
    block = re.search(r"^REGRESSION_CASES=\((.*?)\n\)", text, re.M | re.S).group(1)
    rows = []
    for line in block.splitlines():
        match = re.match(r'\s*"\s*(\w+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*"', line)
        if match:
            key, *nums = match.groups()
            rows.append((key, *map(int, nums)))
    return mapping("DECK_FILE"), mapping("DECK_PROF"), rows


def qc1_select(root, profile, traces, cohort):
    """Classify engine-dealt forced-seven traces; never port the RNG or shuffle.

    The caller's current static profile has no other keep guards. Refuse a changed
    apparatus rather than claiming its keep predicate was reproduced here.
    """
    m = profile.get("mulligan", {})
    required = {"curve_check": "two_drop", "min_lands": 1, "max_lands": 5,
                "min_playable": 0, "required_pieces": [], "min_color_sources": {}, "stop_at": 4}
    if any(m.get(k) != v for k, v in required.items()):
        raise ValueError("QC1 selector requires the documented static mulligan policy; inspect the changed profile")
    if profile.get("keep_model") or profile.get("exhaustive_keep") or profile.get("hand_score_threshold", 0) > -1e6:
        raise ValueError("QC1 selector needs the original static policy with an inactive hand-score threshold")
    cards = {c["name"]: c for c in json.loads((root / "src/cards/data/cards.json").read_text())["cards"]}
    rows = []
    seen = set()
    counts = dict(sampled=0, hard_filter_fail=0, already_passes_two_drop=0, one_land_only=0, no_cheap=0)
    for path in sorted(traces.glob("qc1_sample_s*_gi*.json")):
        t = json.loads(path.read_text())
        name = t.get("deckId", "")
        match = re.fullmatch(r"qc1_sample_s(109000|110000)_off\d+", name)
        if not match:
            continue
        base, gi = int(match.group(1)), int(t["gameNumber"])
        if not 0 <= gi < 300 or int(t["seed"]) != base + gi:
            raise ValueError(f"QC1 sampling seed/index mismatch: {path}")
        if (base, gi) in seen:
            raise ValueError(f"QC1 duplicate sampled identity: {(base, gi)}")
        seen.add((base, gi))
        seq = t.get("mulliganSequence", [])
        if len(seq) != 1 or seq[0].get("attempt") != 0 or not seq[0].get("kept") or len(seq[0].get("hand", [])) != 7:
            raise ValueError(f"QC1 expected a forced original seven: {path}")
        hand = seq[0]["hand"]
        defs = [cards[c["cardName"]] for c in hand]
        lands = sum("Land" in c.get("types", []) for c in defs)
        def mv(c):
            return sum(int(s) if s.isdigit() else 0 if s in ("X", "Y", "Z") else 1
                       for s in re.findall(r"\{([^}]+)\}", c.get("mana_cost", "")))
        cheap = sum("Land" not in c.get("types", []) and mv(c) <= 2 for c in defs)
        cyclers = sum("Creature" in c.get("types", []) and "cycling_cost" in c.get("parameters", {}) for c in defs)
        counts["sampled"] += 1
        if not 1 <= lands <= 5:
            counts["hard_filter_fail"] += 1
            continue
        if lands >= 2 and cheap > 0:
            counts["already_passes_two_drop"] += 1
            continue
        cause = "one_land_only" if lands == 1 else "no_cheap"
        counts[cause] += 1
        # The historical97-hand cohort excludes one-land hands even though
        # they too fail only TwoDrop under the profile's min_lands=1.
        if cohort == "no_cheap" and cause != "no_cheap":
            continue
        rows.append(dict(base_seed=base, game_index=gi, seed=base + gi,
                         land_count=lands, cheap_nonlands=cheap, creature_cyclers=cyclers,
                         cycler_group="c0" if cyclers == 0 else "c1plus", cause=cause,
                         original_hand=hand, source_trace=str(path)))
    if len(seen) != 600:
        raise ValueError(f"QC1 sampling incomplete: found {len(seen)}/600 traces; finish the pooled sample first")
    return rows, counts


def qc1_report(selected, wins_dir, trace_dir=None):
    paired = []
    for row in selected:
        results = {}
        for arm in ("A", "B"):
            name = f"qc1_{arm}_s{row['base_seed']}_gi{row['game_index']}_c{row['creature_cyclers']}"
            p = wins_dir / (name + ".wins")
            parts = p.read_text().splitlines()
            values = [line.split() for line in parts if line.strip() and not line.startswith("#")]
            if len(values) != 1 or int(values[0][0]) != row["game_index"]:
                raise ValueError(f"QC1 missing/unpaired one-game result: {p}")
            wt = int(values[0][1])
            results[arm] = wt if wt > 0 else 9
            if trace_dir is not None:
                trace = json.loads((trace_dir / f"{name}_gi{row['game_index']}.json").read_text())
                first = trace["mulliganSequence"][0]
                if trace["seed"] != row["seed"] or trace["gameNumber"] != row["game_index"] or first["hand"] != row["original_hand"]:
                    raise ValueError(f"QC1 measured hand/shuffle identity changed: {name}")
                if first["kept"] != (arm == "B"):
                    raise ValueError(f"QC1 policy arm did not make its claimed initial decision: {name}")
        paired.append({**row, "A": results["A"], "B": results["B"], "delta": results["B"] - results["A"]})
    groups = {}
    for name, rows in [("all", paired), ("c0", [r for r in paired if r["creature_cyclers"] == 0]),
                       ("c1plus", [r for r in paired if r["creature_cyclers"] >= 1]),
                       ("no_cheap", [r for r in paired if r["cause"] == "no_cheap"]),
                       ("one_land_only", [r for r in paired if r["cause"] == "one_land_only"])]:
        n = len(rows)
        total = sum(r["delta"] for r in rows)
        mean = total / n if n else None
        se = math.sqrt(sum((r["delta"] - mean) ** 2 for r in rows) / (n - 1) / n) if n > 1 else None
        groups[name] = dict(n=n, net_turns=total, delta=mean, se=se,
                            A_mean=sum(r["A"] for r in rows) / n if n else None,
                            B_mean=sum(r["B"] for r in rows) / n if n else None,
                            faster=sum(r["delta"] < 0 for r in rows), slower=sum(r["delta"] > 0 for r in rows))
    return dict(metric="B(curve none)-A(shipped), unwon=9; negative is faster", groups=groups,
                initial_hand_and_keep_decisions_verified=trace_dir is not None, games=paired)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--worktree", type=Path, required=True)
    ap.add_argument("--bin", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--experiment", action="append", choices=["cost", "memory", "pb", "keep", "cast", "payment", "nested", "qc1", "qc3", "verify", "all"])
    ap.add_argument("--chunk", type=int, default=10)
    ap.add_argument("--threads", type=int, default=4)
    ap.add_argument("--cost-flags", default="{}", help="JSON boolean flag map; nonempty means a WHAT-IF cost gate")
    ap.add_argument("--cost-arms", help="optional JSON label->boolean flag-map; overrides --cost-flags, shares one current Hinata control")
    ap.add_argument("--build-mode", choices=["auto", "release", "pgo"], default="auto")
    ap.add_argument("--qc1-traces", type=Path, help="completed forced-seven sampling trace directory; enables selected measurement manifest")
    ap.add_argument("--qc1-cohort", choices=["no_cheap", "all_two_drop"], default="no_cheap",
                    help="no_cheap reproduces ledger's97-hand cohort; all_two_drop also admits one-land hands")
    ap.add_argument("--qc1-results", type=Path, help="completed selected-measurement .wins directory, for grouped paired report")
    ap.add_argument("--qc1-measured-traces", type=Path, help="verify identical initial hands and claimed keep decisions in measured arms")
    a = ap.parse_args()
    root, out, binary = a.worktree.resolve(), a.out.resolve(), a.bin.resolve()
    if out.is_relative_to(root / "decks"):
        ap.error("--out must be outside shipping decks; candidate profiles belong in scratch")
    if a.chunk < 1 or a.threads < 1:
        ap.error("--chunk and --threads must be positive")
    experiments = set(a.experiment or ["cost", "memory"])
    if "all" in experiments:
        experiments = {"cost", "memory", "pb", "keep", "cast", "payment", "nested", "qc1", "qc3", "verify"}
    cost_flags = json.loads(a.cost_flags)
    if not isinstance(cost_flags, dict) or any(not isinstance(v, bool) for v in cost_flags.values()):
        ap.error("--cost-flags must be an object of flag names to JSON booleans")
    cost_arms = json.loads(a.cost_arms) if a.cost_arms else {"whatif" if cost_flags else "live": cost_flags}
    if not isinstance(cost_arms, dict) or not cost_arms or any(not isinstance(v, dict) or any(not isinstance(x, bool) for x in v.values()) for v in cost_arms.values()):
        ap.error("--cost-arms must be a nonempty label->boolean flag-map object")
    build_mode = ("pgo" if "PGO" in binary.parts else "release") if a.build_mode == "auto" else a.build_mode
    out.mkdir(parents=True, exist_ok=True)
    files, profiles, suite = read_suite(root)
    cyc = ("decks/Unpredictable Cyclone/Unpredictable Cyclone.cod",
           "decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json")
    plans, emitted, warnings = [], {}, []
    qc1_sampler_provenance = None
    registry = (root / "src/ai/HeuristicArm.h").read_text()
    profile_json = json.loads((root / cyc[1]).read_text())

    def profile_variant(name, updates):
        source = root / cyc[1]
        dest = out / "profiles" / name
        dest.mkdir(parents=True, exist_ok=True)
        for sibling in source.parent.iterdir():
            if not sibling.is_file() or sibling.name == source.name:
                continue
            link = dest / sibling.name
            if link.exists() or link.is_symlink():
                if not link.is_symlink() or link.resolve() != sibling.resolve():
                    raise ValueError(f"Refusing conflicting candidate sibling: {link}")
            else:
                link.symlink_to(sibling.resolve())
        value = copy.deepcopy(profile_json)
        for k, v in updates.items():
            if isinstance(v, dict):
                value.setdefault(k, {}).update(v)
            else:
                value[k] = v
        target = dest / source.name
        target.write_text(json.dumps(value, indent=2) + "\n")
        return (cyc[0], str(target))

    def job(name, deck_pair, depth, seed, n, budget, pins=None, **extra):
        deck, profile = (str(root / p) for p in deck_pair)
        j = dict(name=name, deck=deck, profile=profile, games=n, seed=seed,
                 game_index=0, budget_ms=budget, max_turns=8,
                 starting_life=20, opponent_heads=1, shuffle_salt=0,
                 shuffle_salt_search=0)
        j.update(extra)
        if depth != 5:
            j.update(depth=depth, ignore_play_profile=True)
        if pins is not None:
            j["flags"] = pins.copy()
        return j

    def chunk_jobs(jobs):
        result = []
        # The same chunk ordinal across arms/profiles receives the same weight.
        # Thus no profile-path grouping can serialize entire arms. These are jobs
        # within one queue, not separately launched waves.
        for j in jobs:
            for off in range(0, j["games"], a.chunk):
                q = dict(j, name=j["name"] + f"_off{off}",
                         seed=j["seed"] + off, game_index=j["game_index"] + off,
                         games=min(a.chunk, j["games"] - off),
                         weight=100000 - off // a.chunk)
                result.append(q)
        return result

    def emit(name, jobs, chunk=True):
        jobs = chunk_jobs(jobs) if chunk else jobs
        path = out / (name + ".json")
        path.write_text(json.dumps({"jobs": jobs}, indent=2) + "\n")
        missing = sorted({key for j in jobs for key in j.get("flags", {}) if '"' + key + '"' not in registry})
        if missing:
            warnings.append({"manifest": str(path), "flags_not_yet_in_worktree_registry": missing})
        absent = sorted({j[key] for j in jobs for key in ("deck", "profile") if not Path(j[key]).is_file()})
        if absent:
            warnings.append({"manifest": str(path), "inputs_not_yet_in_worktree": absent})
        emitted[name] = dict(path=str(path), games=sum(j["games"] for j in jobs), jobs=len(jobs))
        return path

    def plan(name, manifest, threads=None, env=None, trace=False):
        logdir = out / "runs" / name
        base_env = {"MTG_PGO": "1" if build_mode == "pgo" else "0", "MTG_DUMP_UNITS": "1", "MTG_SLOW_GAME_MS": "1"}
        base_env.update(env or {})
        argv = [str(binary), "--batch", str(manifest), "--threads", str(threads or a.threads), "--game-log-dir", str(logdir / "wins")]
        if trace:
            argv += ["--game-trace-dir", str(logdir / "traces")]
        plans.append(dict(name=name, cwd=str(root), env=base_env, argv=argv,
                          stdout=str(logdir / "batch.log"), stderr=str(logdir / "batch.err")))

    if "cost" in experiments:
        jobs = []
        label = next(iter(cost_arms)) if len(cost_arms) == 1 else "arms"
        for arm, arm_flags in cost_arms.items():
            for seed in (2002, 3003):
                jobs += [job(f"cyclone_{arm}_d3_s{seed}", cyc, 3, seed, 40, 10, arm_flags),
                         job(f"cyclone_{arm}_play_s{seed}", cyc, 5, seed, 20, 20, arm_flags)]
            jobs.append(job(f"cyclone2hg_{arm}_d3_s2002", cyc, 3, 2002, 40, 10,
                            arm_flags, starting_life=30, opponent_heads=2))
        # Include current Hinata rows, not historical cached timings or budgets.
        for key, depth, seed, games, budget in suite:
            if key == "hinata" and depth > 0 and budget > 0:
                jobs.append(job(f"hinata_d{depth}_s{seed}", (files[key], profiles[key]), depth, seed, games, budget))
        path = emit("cost_gate_" + label, jobs)
        plan("cost_gate_" + label, path)

    if "memory" in experiments:
        jobs = [job("cyclone_d3_s121000", cyc, 3, 121000, 48, 10),
                job("cyclone_play_s122000", cyc, 5, 122000, 24, 20),
                job("hinata_d3_s123000", (files["hinata"], profiles["hinata"]), 3, 123000, 48, 10),
                job("kitty_d3_s124000", (files["kittyv3"], profiles["kittyv3"]), 3, 124000, 48, 10)]
        path = emit("memory_invariance", jobs, chunk=False)
        env = dict(MTG_PLAN_POOL_STATS="1", MTG_PLAN_CACHE_EXACT_KEY="1",
                   MTG_BP_DERIVE_ISOLATE="1", MTG_CACHE_HIT_CHARGE="1",
                   MTG_FD_ORACLE="1", MTG_FLAG_NONCONV="1")
        # USER steering: keep the original shared RAM-sized pool, with no fixed
        # per-thread quota. Regenerate after the quota-removal patch lands.
        for threads, cache in ((1, "normal"), (6, "normal"), (12, "normal"),
                               (6, "tiny"), (12, "tiny"), (6, "unbounded")):
            cfg = dict(env)
            if cache == "tiny":
                # A genuinely shared 20 MB pool: contention differs by threads,
                # but exact hit/miss behavior must leave decisions/units equal.
                cfg["MTG_PLAN_CACHE_KB"] = "20480"
            if cache == "unbounded":
                cfg.update(MTG_PLAN_CACHE_KB="0")
            plan(f"memory_t{threads}_{cache}", path, threads, cfg)
        shapes = emit("memory_cyclone_shapes", [job("cyclone_d3_s107000", cyc, 3, 107000, 200, 10),
                                                job("cyclone_play_s108000", cyc, 5, 108000, 100, 20)], chunk=False)
        for threads in (6, 12):
            plan(f"memory_shapes_t{threads}", shapes, threads, env)

    if "pb" in experiments:
        arms = {"base": flags(), "PB": flags(CYCLONE_PB_PLANS=True),
                "CYCLER": flags(CYCLONE_CYCLER_RULE=True),
                "PBCYC": flags(CYCLONE_PB_PLANS=True, CYCLONE_CYCLER_RULE=True)}
        jobs = [job(f"{arm}_d{depth}_s{seed}", cyc, depth, seed, n, budget, pins)
                for depth, seed, n, budget in ((3, 109000, 400, 10), (5, 110000, 200, 20))
                for arm, pins in arms.items()]
        path = emit("pb_phase_c", jobs)
        plan("pb_phase_c", path)

    if "keep" in experiments:
        arms = {}
        for prefix, common in (("PB", {"CYCLONE_PB_PLANS": True}),
                               ("PBCYC", {"CYCLONE_PB_PLANS": True, "CYCLONE_CYCLER_RULE": True})):
            arms[prefix] = flags(**common)
            arms[prefix + "_AMEND"] = flags(**common, CYCLONE_KEEP_AMEND=True)
            arms[prefix + "_AMEND_MYRIAD"] = flags(**common, CYCLONE_KEEP_AMEND=True, CYCLONE_KEEP_MYRIAD=True)
        jobs = [job(f"{arm}_d{depth}_s{seed}", cyc, depth, seed, n, budget, pins)
                for depth, seed, n, budget in ((3, 109000, 400, 10), (5, 110000, 200, 20))
                for arm, pins in arms.items()]
        path = emit("keep_phase_c", jobs)
        plan("keep_phase_c", path)

    if "cast" in experiments:
        common = dict(CYCLONE_PB_PLANS=True, CYCLONE_CYCLER_RULE=True)
        arms = {"base": flags(**common), "CO": flags(**common, CYCLONE_CAST_ORDER=True),
                "CO_CYC1": flags(**common, CYCLONE_CAST_ORDER=True, CYCLONE_CYCLE_ORDER=True),
                "CO_CYC1_DRAW": flags(**common, CYCLONE_CAST_ORDER=True, CYCLONE_CYCLE_ORDER=True, CYCLONE_ORDER_DRAW_TURNS=True),
                "CO_CYC1_REV_CYCLERS": flags(**common, CYCLONE_CAST_ORDER=True, CYCLONE_CYCLE_ORDER=True, CYCLONE_CO_REV_CYCLERS=True),
                "CO_CYC1_MIDDLE": flags(**common, CYCLONE_CAST_ORDER=True, CYCLONE_CYCLE_ORDER=True, CYCLONE_CO_CYCLERS_BEFORE_SONG=True)}
        for blind, cells in ((False, ((3, 131000, 200, 10), (5, 132000, 100, 20))),
                             (True, ((3, 135000, 200, 10), (5, 136000, 100, 20)))):
            jobs = [job(f"{arm}_d{depth}_s{seed}", cyc, depth, seed, n, budget, pins,
                        **({"blind_salt": 1} if blind else {}))
                    for depth, seed, n, budget in cells for arm, pins in arms.items()]
            name = "cast_q64_blind" if blind else "cast_q64_raw"
            plan(name, emit(name, jobs))

    if "payment" in experiments:
        arms = {"off": flags(CYCLONE_CYCLER_RULE=True),
                "on": flags(CYCLONE_CYCLER_RULE=True, CYCLE_SEED_PAY_SPLIT=True)}
        # The original full manifest was scratch-only. Reconstruct the documented
        # seeds and conventional 200/100 shape; do not call these historical counts.
        jobs = [job(f"{arm}_d{depth}_s{seed}", cyc, depth, seed, n, budget, pins)
                for depth, seed, n, budget in ((3, 133000, 200, 10), (5, 134000, 100, 20))
                for arm, pins in arms.items()]
        plan("payment_split", emit("payment_split", jobs))

    if "nested" in experiments:
        arms = {"off": flags(CYCLONE_PB_PLANS=True, CYCLONE_CYCLER_RULE=True, LEAF_KILL_ONLY_NOM2=True),
                "on": flags(CYCLONE_PB_PLANS=True, CYCLONE_CYCLER_RULE=True, LEAF_KILL_ONLY_NOM2=True, NESTED_EXH_AXIS=True)}
        jobs = [job(f"{arm}_d{depth}_s{seed}", cyc, depth, seed, n, budget, pins)
                for depth, seed, n, budget in ((3, 129000, 200, 10), (5, 130000, 100, 20))
                for arm, pins in arms.items()]
        plan("nested_exhaustion", emit("nested_exhaustion", jobs), env={"MTG_ROLLOUT_STATS": "1"})

    if "qc3" in experiments:
        arms = {"leaf1": profile_variant("qc3/leaf1", dict(search_leaf_depth=1, search_leaf_first_turn_depth=1)),
                "first1_rest0": profile_variant("qc3/first1_rest0", dict(search_leaf_depth=0, search_leaf_first_turn_depth=1)),
                "leaf0": profile_variant("qc3/leaf0", dict(search_leaf_depth=0, search_leaf_first_turn_depth=0))}
        pins = flags(CYCLONE_PB_PLANS=True, CYCLONE_CYCLER_RULE=True, LEAF_KILL_ONLY_NOM2=True)
        jobs = [job(f"qc3_{arm}_d{depth}_s{seed}", pair, depth, seed, n, budget, pins)
                for depth, seed, n, budget in ((3, 127000, 100, 10), (5, 128000, 60, 20))
                for arm, pair in arms.items()]
        plan("qc3_leaf_fidelity", emit("qc3_leaf_fidelity", jobs))

    if "qc1" in experiments:
        source_prof = root / cyc[1]
        stem = source_prof.name.removesuffix(".profile.json")
        if any(p.is_file() for p in source_prof.parent.glob(stem + ".keepmodel.exhaustive.profile.json*")):
            raise ValueError("QC1 static curve-check experiment must precede keep-table adoption")
        pins = flags(CYCLONE_PB_PLANS=True, CYCLONE_CYCLER_RULE=True, LEAF_KILL_ONLY_NOM2=True)
        sampling = [job(f"qc1_sample_s{seed}", cyc, 0, seed, 300, 0, pins,
                        force_mulligan="0:", max_turns=1) for seed in (109000, 110000)]
        plan("qc1_sample", emit("qc1_sample", sampling), trace=True)
        A = profile_variant("qc1/shipped", {})
        B = profile_variant("qc1/curve_none", dict(mulligan={"curve_check": "none"}))
        if a.qc1_traces:
            sampler_meta = a.qc1_traces.resolve().parent / "meta.json"
            if sampler_meta.is_file():
                recorded = json.loads(sampler_meta.read_text())
                for path, expected in recorded.get("inputs", {}).items():
                    actual = hashlib.sha256((root / path).read_bytes()).hexdigest()
                    if actual != expected:
                        raise ValueError(f"QC1 original sampler source/data changed: {path}")
                qc1_sampler_provenance = dict(path=str(sampler_meta), recorded=recorded)
            selected, counts = qc1_select(root, profile_json, a.qc1_traces.resolve(), a.qc1_cohort)
            (out / "qc1-selection.json").write_text(json.dumps(dict(cohort=a.qc1_cohort, counts=counts, selected=selected), indent=2) + "\n")
            if not selected:
                raise ValueError("QC1 complete sample contains no eligible hands")
            jobs = []
            for row in selected:
                for arm, pair in (("A", A), ("B", B)):
                    name = f"qc1_{arm}_s{row['base_seed']}_gi{row['game_index']}_c{row['creature_cyclers']}"
                    j = job(name, pair, 3, row["seed"], 1, 10, pins)
                    j["game_index"] = row["game_index"]
                    # No force_mulligan in either measured policy arm.
                    jobs.append(j)
            plan("qc1_keep_vs_mulligan", emit("qc1_keep_vs_mulligan", jobs, chunk=False), trace=True)
            if a.qc1_results:
                report = qc1_report(selected, a.qc1_results.resolve(),
                                    a.qc1_measured_traces.resolve() if a.qc1_measured_traces else None)
                (out / "qc1-report.json").write_text(json.dumps(report, indent=2) + "\n")
        else:
            warnings.append({"qc1": "Run the600-game forced-seven d0 sampling queue with traces, then regenerate with --qc1-traces. This sampling->selection dependency is intentional; measured arms will not force hands."})

    if "verify" in experiments:
        jobs = []
        for arm, amend in (("plain", False), ("amend", True)):
            pins = flags(CYCLONE_PB_PLANS=True, CYCLONE_CYCLER_RULE=True,
                         LEAF_KILL_ONLY_NOM2=True, CYCLONE_KEEP_AMEND=amend, CYCLONE_KEEP_MYRIAD=amend)
            for depth, base, count, budget in ((3, 122000, 8, 10), (5, 123000, 4, 20)):
                off = count if amend else 0
                j = job(f"verify_{arm}_d{depth}", cyc, depth, base + off, count, budget, pins)
                j["game_index"] = off
                jobs.append(j)
        env = {"MTG_CYC_JUNK_MEMO": "1", "MTG_CYC_JUNK_MEMO_VERIFY": "1",
               "MTG_CANTRIP_RANK_CACHE": "1", "MTG_CANTRIP_RANK_CACHE_VERIFY": "1",
               "MTG_CANTRIP_ORDER_COST": "1", "MTG_LEAF_KILL_ONLY": "1", "MTG_LEAF_KILL_ONLY_VERIFY": "1",
               "MTG_ROLLOUT_STATS": "1"}
        plan("verify_small", emit("verify_small", jobs, chunk=False), env=env)

    metadata = dict(worktree=str(root), commit=git(root, "rev-parse", "HEAD"),
                    src_tree=git(root, "rev-parse", "HEAD:src"),
                    binary=str(binary), binary_mode=build_mode,
                    binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest() if binary.is_file() else None,
                    emitted=emitted, warnings=warnings, plans=plans,
                    qc1_sampler_provenance=qc1_sampler_provenance,
                    unset_before_run=sorted(set(BASE) | PROCESS_CACHE_OVERRIDES | {"MTG_BOTTOM_ROLLOUTS", "MTG_EXHAUSTIVE_PROFILE", "MTG_EXHAUSTIVE_BOTTOM",
                                      "MTG_PLAN_CACHE_KB", "MTG_PLAN_CACHE_THREAD_KB", "MTG_PLAN_CACHE_PER_THREAD", "MTG_FSL_PER_THREAD",
                                      "MTG_SEARCH_BLIND_UNKNOWN", "MTG_SEARCH_BLIND_SALT", "MTG_SHUFFLE_SALT_OPENING", "MTG_FD_LEAF_DEPTH",
                                      "MTG_FLAG_NONCONV", "MTG_FD_ORACLE", "MTG_CYCLE_SEED_PAY_CENSUS",
                                      "MTG_CYC_JUNK_MEMO_VERIFY", "MTG_CANTRIP_RANK_CACHE_VERIFY", "MTG_LEAF_KILL_ONLY_VERIFY", "MTG_CANTRIP_ORDER_COST"}),
                    note="Generate-only. Review flags/inputs and ensure a quiet box before timing. All input paths are absolute; run cwd is worktree. Memory runs require the fixed-quota removal patch, preserving original shared RAM budgets. Remove inherited quota env first. No approval/adoption is implied.")
    (out / "run-plan.json").write_text(json.dumps(metadata, indent=2) + "\n")
    lines = ["# Generated validation inputs", "", "No commands were launched.", "",
             "Use run-plan.json for exact cwd, argv, environment, and log paths.",
             "Create log-parent directories before launching. Unset the listed inherited controls first.",
             "Memory process variants are genuinely independent scheduling/cache experiments; each reuses the identical manifest.",
             "The cost gate uses live defaults unless --cost-flags supplies a labelled what-if.", ""]
    for name, data in emitted.items():
        lines.append(f"- {name}: {data['games']} games, {data['jobs']} jobs; {data['path']}")
    lines += ["", "Warnings:", json.dumps(warnings, indent=2), "", "Example command forms (not launched):", ""]
    for p in plans:
        lines.append(shlex.join(["env", *[k + "=" + v for k, v in p["env"].items()], *p["argv"]]))
    (out / "README.md").write_text("\n".join(lines) + "\n")
    print(json.dumps({"emitted": emitted, "warnings": warnings}, indent=2))


if __name__ == "__main__":
    main()
