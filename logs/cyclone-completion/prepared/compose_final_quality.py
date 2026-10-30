#!/usr/bin/env python3
"""Compose prepared independent Cyclone quality work; never launch simulations.

Requires freshly generated make_validation_manifests output plus the pruning
reviewer's phaseC manifest. The quiet cost gate stays separate and receives no
trace logging. Process-static memory pressure configs also remain independent
experiments; normal12 reference games live in this primary pool.
"""
import argparse
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path

FILES = ["qc1_keep_vs_mulligan.json", "qc3_leaf_fidelity.json", "nested_exhaustion.json",
         "payment_split.json", "cast_q64_raw.json", "cast_q64_blind.json",
         "memory_invariance.json", "memory_cyclone_shapes.json"]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--inputs", type=Path, required=True)
    ap.add_argument("--prunes", type=Path, required=True)
    ap.add_argument("--worktree", type=Path, required=True)
    ap.add_argument("--bin", type=Path, required=True)
    ap.add_argument("--threads", type=int, default=12)
    ap.add_argument("--seal", action="store_true", help="require clean final source and a current PGO stamp/binary")
    a = ap.parse_args()
    root, out, binary = a.worktree.resolve(), a.inputs.resolve(), a.bin.resolve()
    def git(*args):
        return subprocess.check_output(["git", "-C", str(root), *args], text=True,
                                       env={**os.environ, "GIT_OPTIONAL_LOCKS": "0"}).strip()
    own_plan = json.loads((out / "run-plan.json").read_text())
    if own_plan.get("warnings"):
        raise ValueError("Regenerate inputs after resolving their warnings")
    if own_plan["worktree"] != str(root):
        raise ValueError("Input worktree mismatch")
    source_status = git("status", "--short", "--", "src")
    stamp = binary.parent / "SRC_TREE"
    src_tree = git("rev-parse", "HEAD:src")
    if a.seal and (source_status or not binary.is_file() or not stamp.is_file() or stamp.read_text().strip() != src_tree):
        raise ValueError("Cannot seal until source is clean and binary/PGO SRC_TREE stamp matches HEAD:src")
    if a.seal:
        if own_plan["src_tree"] != src_tree:
            raise ValueError("Regenerate QC/cast/payment inputs on the final frozen source before sealing")
        source_digest = hashlib.sha256()
        for name in sorted(git("ls-files", "--", "src").splitlines()):
            data = (root / name).read_bytes()
            source_digest.update(name.encode() + b"\0" + str(len(data)).encode() + b"\0" + data)
        prunes_meta = json.loads((a.prunes.resolve().parent / "meta.json").read_text())
        if prunes_meta["src_worktree_sha256"] != source_digest.hexdigest():
            raise ValueError("Pruning provenance predates final source; refresh it before any measurements start")
        profile_path = root / "decks/Unpredictable Cyclone/Unpredictable Cyclone.profile.json"
        if prunes_meta["profile_sha256"] != hashlib.sha256(profile_path.read_bytes()).hexdigest():
            raise ValueError("Pruning profile provenance differs from the final apparatus")
    jobs, maps = [], {}
    sources = [a.prunes.resolve(), *[out / file for file in FILES]]
    for source in sources:
        family = source.stem if source.parent == out else "phasec"
        for j in json.loads(source.read_text())["jobs"]:
            if "force_mulligan" in j:
                raise ValueError("Sampling/forced-hand job cannot enter measured quality pool")
            # Main pool reference games retain the standalone memory cell names
            # and indices for straightforward comparison. Chunk them solely to
            # interleave this work rather than leaving a memory-only tail.
            chunks = []
            if source.name in ("memory_invariance.json", "memory_cyclone_shapes.json"):
                for off in range(0, j["games"], 10):
                    chunks.append(dict(j, name=j["name"] + f"_off{off}",
                                       seed=j["seed"] + off, game_index=j["game_index"] + off,
                                       games=min(10, j["games"] - off), weight=100000 - off // 10))
            else:
                chunks.append(j)
            for q in chunks:
                name = q["name"]
                if name in maps:
                    raise ValueError(f"Duplicate job name: {name}")
                maps[name] = dict(family=family, source_manifest=str(source),
                                  logical_cell=re.sub(r"_off\d+$", "", name),
                                  seed=q["seed"], first_gi=q.get("game_index", 0), games=q["games"])
                jobs.append(q)
    manifest = out / "primary-quality.json"
    text = json.dumps({"jobs": jobs}, indent=2) + "\n"
    if a.seal and manifest.exists() and manifest.read_text() != text:
        raise ValueError("Sealing cannot replace different prepared jobs; inspect/regenerate first")
    manifest.write_text(text)
    run_dir = out / "runs" / "primary-quality"
    source_sha = hashlib.sha256(git("diff", "--binary", "HEAD", "--", "src").encode()).hexdigest()
    env = {"MTG_PGO": "1", "MTG_DUMP_UNITS": "1", "MTG_SLOW_GAME_MS": "1", "MTG_PLAN_POOL_STATS": "1",
           "MTG_ROLLOUT_STATS": "1", "MTG_FLAG_NONCONV": "1", "MTG_FD_ORACLE": "1",
           "MTG_CYCLE_SEED_PAY_CENSUS": "1"}
    argv = [str(binary), "--batch", str(manifest), "--threads", str(a.threads),
            "--game-log-dir", str(run_dir / "wins"), "--game-trace-dir", str(run_dir / "traces")]
    plan = dict(sealed=a.seal, cwd=str(root), commit=git("rev-parse", "HEAD"), src_tree=src_tree,
                src_status=source_status, src_patch_sha256=source_sha,
                binary=str(binary), binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest() if binary.is_file() else None,
                manifest_sha256=hashlib.sha256(text.encode()).hexdigest(),
                games=sum(j["games"] for j in jobs), jobs=len(jobs), env=env, argv=argv,
                unset_before_run=sorted(set(own_plan["unset_before_run"]) | {"MTG_DIAG_PB_T1_ORDER"}),
                stdout=str(run_dir / "batch.log"), stderr=str(run_dir / "batch.err"), job_map=maps,
                input_manifest_sha256={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
                analysis="Full traces support per-game root causes; do not read mixed quality-pool timings as the cost gate. Aggregate chunk outcomes by logical cell. Prunes report can read this unique-name shared wins directory through its documented path.")
    (out / "primary-quality-plan.json").write_text(json.dumps(plan, indent=2) + "\n")
    # One combined six-worker reference pool covers both originally owed
    # normal6 datasets. Normal12 is already in the primary pool. Tiny6 repeats
    # tiny12's coverage and is the sole removed redundant cache experiment.
    six = out / "memory-six-reference.json"
    six.write_text(json.dumps({"jobs": [
        *json.loads((out / "memory_invariance.json").read_text())["jobs"],
        *json.loads((out / "memory_cyclone_shapes.json").read_text())["jobs"]]}, indent=2) + "\n")
    standalone = {p["name"]: p for p in own_plan["plans"]}
    other = [standalone[n] for n in ("memory_t1_normal", "memory_t12_tiny", "memory_t6_unbounded")]
    p6 = dict(standalone["memory_t6_normal"])
    p6["name"] = "memory_t6_both_reference_sets"
    p6["argv"] = list(p6["argv"])
    p6["argv"][p6["argv"].index("--batch") + 1] = str(six)
    other.insert(1, p6)
    cost = [p for p in own_plan["plans"] if p["name"].startswith("cost_gate_")]
    verify = [p for p in own_plan["plans"] if p["name"] == "verify_small"]
    schedule = dict(primary_quality=plan, memory_comparisons=other, quiet_cost=cost, verify_after_main_and_cost=verify,
                    order="Run independent memory comparisons and primary quality on the same frozen PGO source; cost only when every other pool is finished and box is quiet. Data-dependent pruning blind and per-game recovery work follow primary outcomes.",
                    removed_redundancy="Do not run old pb_phase_c/keep_phase_c: imported12-arm pruning set shares those controls. Normal12 memory and shapes are included in primary; normal6 sets combined into one queue; tiny6 omitted as redundant with tiny12. Do not re-run QC1 sampler:600 complete traces have matching setup/library/cards/deck hashes.")
    (out / "final-runtime-plan.json").write_text(json.dumps(schedule, indent=2) + "\n")
    families = {}
    for m in maps.values():
        families[m["family"]] = families.get(m["family"], 0) + m["games"]
    print(json.dumps(dict(manifest=str(manifest), jobs=len(jobs), games=plan["games"], families=families,
                          sealed=a.seal, source_dirty=bool(source_status)), indent=2))


if __name__ == "__main__":
    main()
