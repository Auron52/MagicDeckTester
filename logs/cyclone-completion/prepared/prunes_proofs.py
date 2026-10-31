#!/usr/bin/env python3
"""Trace eligibility and conditional follow-up inputs; never launches the engine.

iso92: validate the two forty-salt outputs, report all pairs and the subset
whose same T1 state actually takes the intended Brainstorm order in BOTH arms.

movers: inspect completed Phase C traces symmetrically, list first observable
divergences, prepare deduplicated paired d8/b0 recovery jobs, and optionally
prepare forty-salt whole-game blind pairs for explicitly selected cases.
Neither conditional set is a deck-wide effect or an isolated-decision oracle.
"""

import argparse
import contextlib
import copy
import hashlib
import io
import json
from pathlib import Path
import sys

import reconstruct_prunes as rp


EMRAKUL = "Emrakul, the Aeons Torn"
GYRE = "Ulamog, the Infinite Gyre"


def write(path, value):
    rp.dump(path, value)


def cards(value):
    return [(c.get("card"), c.get("cardName")) for c in value]


def events(trace):
    rows = []
    for phase_no, phase in enumerate(trace.get("turns", [])):
        context = (phase.get("turn"), phase.get("extraTurn", 0), phase.get("phase"))
        for action_no, action in enumerate(phase.get("actions", [])):
            rows.append({"phase_index": phase_no, "action_index": action_no,
                         "context": context, "action": action})
    return rows


def trace_identity(trace, seed, gi):
    if trace.get("seed") != seed or trace.get("gameNumber") != gi:
        raise ValueError(f"trace identity mismatch: expected seed{seed}/gi{gi}, got {trace.get('seed')}/{trace.get('gameNumber')}")


def trace_result(trace):
    value = trace.get("result", {})
    return value.get("turn", -1) if value.get("winner") == "player" else -1


def t1_brainstorm(trace):
    """Return the first T1 Brainstorm resolution with enough evidence to isolate it."""
    flat = events(trace)
    puts = [i for i, row in enumerate(flat)
            if tuple(row["context"]) == (1, 0, "MAIN_1")
            and row["action"].get("type") == "REVEAL"
            and row["action"].get("source") == "Brainstorm (put back)"]
    if len(puts) != 1:
        return None, f"expected one T1 Brainstorm put-back; observed {len(puts)}"
    put_i = puts[0]
    cast_i = next((i for i in range(put_i - 1, -1, -1)
                   if flat[i]["context"] == flat[put_i]["context"]
                   and flat[i]["action"].get("type") == "CAST_SPELL"
                   and flat[i]["action"].get("cardName") == "Brainstorm"), None)
    if cast_i is None:
        return None, "put-back has no preceding same-phase Brainstorm cast"
    between = flat[cast_i + 1:put_i]
    if any(x["action"].get("type") != "DRAW" for x in between):
        return None, "another observable action occurred between cast and put-back"
    put = flat[put_i]["action"]
    placed = cards(put.get("lookedAt", []))
    if len(placed) != 2 or len({x[0] for x in placed}) != 2 or any(not isinstance(x[0], int) or x[0] <= 0 for x in placed):
        return None, "put-back lacks two distinct physical card identities"
    if put.get("kept") != [x[0] for x in placed] or put.get("bottomed"):
        return None, "put-back annotation disagrees with its top-first placed sequence"
    return {
        "placed": placed,
        "names": [x[1] for x in placed],
        # Compare everything before the cast, including previous phase actions,
        # plus the cast itself (card identity and mana bill). Draw logging may
        # order drawn-and-put-back cards differently, so compare its multiset.
        "prefix": [{"context": x["context"], "action": x["action"]} for x in flat[:cast_i + 1]],
        "drawn": sorted(cards([x["action"] for x in between])),
        "event": flat[put_i],
    }, None


def isolated_eligibility(normal, reversed_trace):
    reasons = []
    if not normal.get("openingHand") or not reversed_trace.get("openingHand"):
        reasons.append("opening-hand evidence is missing")
    if not normal.get("mulliganSequence") or not reversed_trace.get("mulliganSequence"):
        reasons.append("mulligan-sequence evidence is missing")
    if cards(normal.get("openingHand", [])) != cards(reversed_trace.get("openingHand", [])):
        reasons.append("opening hands differ by physical identity/order")
    if normal.get("mulliganSequence") != reversed_trace.get("mulliganSequence"):
        reasons.append("mulligan sequence differs before the put-back")
    a, ar = t1_brainstorm(normal)
    b, br = t1_brainstorm(reversed_trace)
    if ar:
        reasons.append("normal: " + ar)
    if br:
        reasons.append("reversed: " + br)
    if a and b:
        if a["names"] != [GYRE, EMRAKUL]:
            reasons.append("normal did not put Gyre first and Emrakul second")
        if b["names"] != [EMRAKUL, GYRE]:
            reasons.append("reversed did not put Emrakul first and Gyre second")
        if a["placed"] != list(reversed(b["placed"])):
            reasons.append("the two put-back pairs are not the same physical cards reversed")
        if a["prefix"] != b["prefix"]:
            reasons.append("observable T1 prefix/cast differs before the intervention")
        if a["drawn"] != b["drawn"]:
            reasons.append("Brainstorm drew different physical cards")
    return {"eligible": not reasons, "reasons": reasons,
            "normal_putback": a["placed"] if a else None,
            "reversed_putback": b["placed"] if b else None}


def iso92_report(stage):
    salts = json.loads((stage / "meta.json").read_text()).get("salts", list(range(1, 41)))
    if len(salts) != 40 or len(set(salts)) != 40:
        raise ValueError("iso92 requires forty distinct paired blind salts")
    rows = []
    for salt in salts:
        name = f"pb_d3_gi92_bs{salt}"
        traces, outcomes = {}, {}
        for arm in ("normal", "reversed"):
            values = rp.read_wins(stage / arm / "wins" / (name + ".wins"))
            if set(values) != {92}:
                raise ValueError(f"incomplete iso92 output: {arm} salt{salt}")
            path = stage / arm / "traces" / (name + "_gi92.json")
            trace = json.loads(path.read_text())
            trace_identity(trace, 109092, 92)
            if trace_result(trace) != values[92][0]:
                raise ValueError(f"trace/result disagree: {path}")
            traces[arm], outcomes[arm] = trace, values[92]
        eligibility = isolated_eligibility(traces["normal"], traces["reversed"])
        rows.append({"salt": salt, **eligibility,
                     "normal_score": rp.score(outcomes["normal"]),
                     "reversed_score": rp.score(outcomes["reversed"]),
                     "delta": rp.score(outcomes["reversed"]) - rp.score(outcomes["normal"])})
    eligible = [x for x in rows if x["eligible"]]
    def summary(part):
        return {**rp.brief(x["delta"] for x in part),
                "normal_mean": sum(x["normal_score"] for x in part) / len(part) if part else None,
                "reversed_mean": sum(x["reversed_score"] for x in part) / len(part) if part else None}
    return {
        "direction": "reversed old Emrakul-first minus normal fixed Gyre-first; positive favours fix",
        "unwon_score": 9,
        "all_salts": summary(rows), "forced_same_T1_state_subset": summary(eligible),
        "proof_status": "eligible_pairs_available_for_review" if eligible else "UNPROVEN_no_eligible_same_T1_pairs",
        "eligible_salts": [x["salt"] for x in eligible],
        "excluded_salts": [x["salt"] for x in rows if not x["eligible"]],
        "scope": "conditional isolated T1-order test on gi92; neither subset nor all salts is a deck-wide effect",
        "eligibility": "same physical opening/mulligan sequence, observable prefix and cast, drawn multiset, and exactly reversed physical Eldrazi pair",
        "limitations": "The general blind mode has no real-resolution seed-scry/free-cast chooser; both arms share that limitation.",
        "rows": rows,
    }


def divergence(a, b):
    if a.get("mulliganSequence") != b.get("mulliganSequence"):
        return {"kind": "mulligan", "baseline": a.get("mulliganSequence"), "arm": b.get("mulliganSequence")}
    if cards(a.get("openingHand", [])) != cards(b.get("openingHand", [])):
        return {"kind": "opening_hand", "baseline": a.get("openingHand"), "arm": b.get("openingHand")}
    aa, bb = events(a), events(b)
    for i in range(max(len(aa), len(bb))):
        x, y = aa[i] if i < len(aa) else None, bb[i] if i < len(bb) else None
        if x and y and x["context"] == y["context"] and x["action"] == y["action"]:
            continue
        kind = "other"
        actions = [z["action"] for z in (x, y) if z]
        sources = " ".join(str(z.get("source", "")) for z in actions).lower()
        if "(put back)" in sources:
            kind = "putback"
        elif "ponder" in sources:
            kind = "ponder"
        elif "scry" in sources:
            kind = "scry"
        elif any(z.get("type") == "ABILITY" and "cycl" in str(z.get("ability", "")).lower() for z in actions):
            kind = "cycle"
        elif any(z.get("type") == "PLAY_LAND" for z in actions):
            kind = "land"
        elif any(z.get("type") == "CAST_SPELL" for z in actions):
            kind = "cast"
        elif any(z.get("type") == "DRAW" for z in actions):
            kind = "draw"
        return {"kind": kind, "event_index": i, "baseline": x, "arm": y,
                "interpretation": "first observable trace difference, not an automatic causal verdict"}
    return {"kind": "no_action_difference", "interpretation": "inspect board snapshots, hidden state, and digest inputs"}


def primary_control(arm, arms):
    return rp.primary_control(arm, arms)


def mover_inputs(root, traces, output, selected_arms, expand_cases, salts):
    phase = root / "phasec"
    meta = json.loads((phase / "meta.json").read_text())
    now = rp.provenance(Path(meta["worktree"]))
    for key in ("src_worktree_sha256", "profile_sha256"):
        if meta.get(key) != now[key]:
            raise ValueError(f"source/apparatus changed ({key}); conditional proof cannot mix trees")
    manifest = json.loads((phase / "manifest.json").read_text())
    with contextlib.redirect_stdout(io.StringIO()):
        arms, reports = rp.phase_report(root)
    if selected_arms is None:
        selected_arms = [a for a in arms if a not in ("base", "cycler")]
    if any(a not in arms or a == "base" for a in selected_arms):
        raise ValueError("selected arms must be measured non-base Phase C arms")
    lookup = {}
    for item in manifest["jobs"]:
        arm, depth = rp.phase_arm(item), item["depth"]
        for gi in range(item["game_index"], item["game_index"] + item["games"]):
            lookup[arm, depth, gi] = item
    rows, recovery = [], {}
    win_cache = {}
    def original_trace(arm, depth, gi):
        item = lookup[arm, depth, gi]
        path = traces / f"{item['name']}_gi{gi}.json"
        value = json.loads(path.read_text())
        trace_identity(value, rp.CELLS[depth][0] + gi, gi)
        if item["name"] not in win_cache:
            win_cache[item["name"]] = rp.read_wins(phase / "wins" / (item["name"] + ".wins"))
        if trace_result(value) != win_cache[item["name"]][gi][0]:
            raise ValueError(f"trace/result disagree: {path}")
        return value, path
    def one_job(arm, depth, gi, name, recovery_mode=False, salt=None):
        src = lookup[arm, depth, gi]
        out = copy.deepcopy(src)
        out.update(name=name, games=1, seed=rp.CELLS[depth][0] + gi, game_index=gi)
        if recovery_mode:
            out.update(depth=8, budget_ms=0, ignore_play_profile=True)
        if salt is not None:
            out["blind_salt"] = salt
        return out
    for depth in rp.CELLS:
        for arm in selected_arms:
            control = primary_control(arm, arms)
            report = reports[f"{arm}-vs-{control}_d{depth}"]
            for direction, indices in (("slower", report["slower_gi"]), ("faster", report["faster_gi"])):
                for gi in indices:
                    a, ap = original_trace(control, depth, gi)
                    b, bp = original_trace(arm, depth, gi)
                    case = f"{arm}-vs-{control}_d{depth}_gi{gi}"
                    rows.append({"case": case, "arm": arm, "control": control, "depth": depth,
                                 "gi": gi, "seed": rp.CELLS[depth][0] + gi, "direction": direction,
                                 "baseline_score": rp.score((trace_result(a),)), "arm_score": rp.score((trace_result(b),)),
                                 "baseline_trace": str(ap), "arm_trace": str(bp),
                                 "first_observable_difference": divergence(a, b),
                                 "limited_information": "Unclassified: inspect direct decision versus prior budget/mulligan divergence; whole-game blind replay alone does not isolate a decision."})
                    if direction == "slower":
                        for which in (control, arm):
                            key = which, depth, gi
                            if key not in recovery:
                                recovery[key] = one_job(which, depth, gi, f"recover_prunes_{which}_fromd{depth}_gi{gi}", recovery_mode=True)
    cases = {x["case"]: x for x in rows}
    absent = set(expand_cases) - cases.keys()
    if absent:
        raise ValueError(f"unknown selected expansion case(s): {sorted(absent)}")
    expanded = {}
    for case in expand_cases:
        row = cases[case]
        for salt in range(1, salts + 1):
            for arm in (row["control"], row["arm"]):
                key = arm, row["depth"], row["gi"], salt
                if key not in expanded:
                    expanded[key] = one_job(arm, row["depth"], row["gi"],
                        f"expanded_prunes_{arm}_d{row['depth']}_gi{row['gi']}_bs{salt}", salt=salt)
    scope = "conditional on raw outcome movers for each contrast; symmetric slower AND faster selection; not a deck-wide effect"
    write(output / "movers-review.json", {"scope": scope, "arms": selected_arms, "rows": rows})
    write(output / "recovery.json", {"jobs": list(recovery.values())})
    write(output / "blind-expanded.json", {"jobs": list(expanded.values())})
    write(output / "meta.json", {**now, "scope": scope, "phase_manifest_sha256": hashlib.sha256((phase / "manifest.json").read_bytes()).hexdigest(),
          "mover_cases": len(rows), "slower_cases": sum(x["direction"] == "slower" for x in rows),
          "recovery_jobs": len(recovery), "expanded_cases": expand_cases, "expanded_blind_jobs": len(expanded),
          "expanded_salts": salts, "limitations": [
              "recovery.json is paired d8/b0; no abandonment or time cap. Review its cost before launch.",
              "blind-expanded.json uses the SAME arm/control initial seed and salt, not a forced mid-game decision.",
              "It does NOT by itself prove an isolated decision is right or wrong; check trace eligibility/prefixes.",
              "No real-resolution chooser for cycle-seed scry/free-cast pins in the general blind mode.",
              "For aggregate blind screening use reconstruct_prunes.py blind --salts 4; do not repeat its jobs here."]})
    return {"mover_cases": len(rows), "recovery_games": len(recovery), "expanded_blind_games": len(expanded), "scope": scope}


def expanded_report(inputs, wins, traces):
    meta = json.loads((inputs / "meta.json").read_text())
    cases = {x["case"]: x for x in json.loads((inputs / "movers-review.json").read_text())["rows"]}
    reports = {}
    for case in meta["expanded_cases"]:
        row, paired = cases[case], []
        for salt in range(1, meta["expanded_salts"] + 1):
            results, state = {}, {}
            for arm in (row["control"], row["arm"]):
                name = f"expanded_prunes_{arm}_d{row['depth']}_gi{row['gi']}_bs{salt}"
                value = rp.read_wins(wins / (name + ".wins"))
                if set(value) != {row["gi"]}:
                    raise ValueError(f"incomplete expanded pair: {name}")
                path = traces / f"{name}_gi{row['gi']}.json"
                trace = json.loads(path.read_text())
                trace_identity(trace, row["seed"], row["gi"])
                if trace_result(trace) != value[row["gi"]][0]:
                    raise ValueError(f"trace/result disagree: {path}")
                results[arm], state[arm] = value[row["gi"]], trace
            a, b = state[row["control"]], state[row["arm"]]
            paired.append({"salt": salt, "delta": rp.score(results[row["arm"]]) - rp.score(results[row["control"]]),
                           "same_physical_opening_hand": cards(a.get("openingHand", [])) == cards(b.get("openingHand", [])),
                           "same_mulligan_sequence": a.get("mulliganSequence") == b.get("mulliganSequence"),
                           "first_observable_difference": divergence(a, b)})
        reports[case] = {**rp.brief(x["delta"] for x in paired),
                         "raw_delta": row["arm_score"] - row["baseline_score"],
                         "same_opening_hand_pairs": sum(x["same_physical_opening_hand"] for x in paired),
                         "same_mulligan_pairs": sum(x["same_mulligan_sequence"] for x in paired),
                         "rows": paired}
    return {"scope": "selected conditional whole-game paired blind outcomes; NOT a deck-wide effect or a forced same-state decision proof",
            "proof_status": "REQUIRES_MANUAL_CAUSAL_REVIEW_even_when_blind_mean_favours_an_arm",
            "limitations": meta["limitations"], "cases": reports}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, required=True, help="final-prunes directory")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("iso92")
    movers = sub.add_parser("movers")
    movers.add_argument("--traces", type=Path, required=True, help="completed primary-quality full traces directory")
    movers.add_argument("--out", type=Path, help="fresh output directory; default root/proofs")
    movers.add_argument("--arms", help="comma-separated measured arms; default PB/KEEP/SITU/SEED/Ponder contrasts")
    movers.add_argument("--expand-cases", default="", help="comma-separated reviewed case ids for paired whole-game blind expansion")
    movers.add_argument("--salts", type=int, default=40)
    expanded = sub.add_parser("expanded-report")
    expanded.add_argument("--inputs", type=Path, required=True, help="movers output directory containing the selected expansion")
    expanded.add_argument("--wins", type=Path, required=True)
    expanded.add_argument("--traces", type=Path, required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    if args.command == "iso92":
        report = iso92_report(root / "iso92")
        write(root / "iso92/trace-eligibility-report.json", report)
        print(json.dumps({k: v for k, v in report.items() if k != "rows"}, indent=2))
    elif args.command == "movers":
        if args.salts < 1:
            raise ValueError("--salts must be positive")
        arms = args.arms.split(",") if args.arms else None
        expand = args.expand_cases.split(",") if args.expand_cases else []
        if len(set(expand)) != len(expand):
            raise ValueError("expanded case ids must be distinct")
        report = mover_inputs(root, args.traces.resolve(), args.out.resolve() if args.out else root / "proofs", arms, expand, args.salts)
        print(json.dumps(report, indent=2))
    else:
        report = expanded_report(args.inputs.resolve(), args.wins.resolve(), args.traces.resolve())
        write(args.inputs.resolve() / "expanded-report.json", report)
        summary = {k: v for k, v in report.items() if k != "cases"}
        summary["cases"] = {k: {x: y for x, y in v.items() if x != "rows"} for k, v in report["cases"].items()}
        print(json.dumps(summary, indent=2))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError) as error:
        print(f"error: {error}", file=sys.stderr)
        sys.exit(2)
