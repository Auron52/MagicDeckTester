#!/usr/bin/env python3
"""Manifest generator for the escalation-beam depth-gate A/B (2026-11-24).

USER (2026-11-24): "It does sound weird that we act differently at depth 5, so I would be up for changing that for
sure. The only question would be results I suppose. I do still have a slight preference for not having additional
per-deck settings, but if we don't have a choice to get good results I could be convinced."

ARMS (one binary, per-job heurarm flags; see src/ai/TurnSolver.cpp "DEPTH-ADAPTIVE BEAM"):
  A  origin: depth==5 -> the deck's value_play beam (value-ranked, beam_width/beam_leafdepth; off when width 0);
             depth==3 -> static W20 ld1 for every driving deck; every other depth -> off.
  B  MTG_ESC_BEAM_NONE=1: no beam at any depth (the code path deleted; beam_width ignored).
  C  MTG_ESC_BEAM_STATIC_ALL=1: the d3 static W20 ld1 beam at EVERY depth for every driving deck (beam_width ignored).

Only decks whose value_play block DRIVES (enabled, target_depth > 0) can ever beam (AIEngine passes beam_width -1
otherwise), so only they are rows. Per (deck, setting) the arms are grouped by the beam regime they resolve to; an
arm whose regime equals an earlier arm's is IDENTICAL BY CONSTRUCTION (same eff_beam / leafdepth / static => same
code path, same digests) and is not re-run: equiv.json maps it to the arm that represents it. Examples: a deck whose
play depth is 5 with no beam_width (Hinata, Burn, ...) has A == B at play; at d3 A == C for every deck; at d4/d6/d7
A == B.

Rows, in order (PLAY SETTINGS FIRST; no d0 rows -- the beam is search-only):
  play       depth/budget omitted -> the deck's value_play block picks them (the [play] line says "value_play").
  play 2HG   the same, starting_life 30 + opponent_heads 2, for every deck with a "<key>2hg" suite row.
  d4 / d3    pinned (--ignore-play-profile): d4 at the deck's play budget, d3 at b10 (the suite's d3 shape).

Seeds: fresh block from BASE (6,400,001 default -- no manifest under build/scratch and no doc uses 6.4M-6.49M; the
2.86M/2.87M range the brief suggested IS used, see logs/beam-gate/handoff.md). Each job is <= CHUNK games at
seed = BASE + 1000*k, game_index 0; the SAME (seed, game_index) block is used by every arm of a (deck, setting), so
arms pair per game. Every (deck, setting, chunk) gets its own k, so no two rows share a game seed.

DO NOT RUN THIS BATCH YOURSELF (coordinator runs every batch > ~10 games under build/scratch/claude_box.lock).
usage: mk_ab.py [--out DIR] [--base N] [--core-games N] [--fleet-games N] [--no-fleet] [--repo PATH]
"""
import argparse, json, os, subprocess, sys

ARMS = {"A": {}, "B": {"MTG_ESC_BEAM_NONE": True}, "C": {"MTG_ESC_BEAM_STATIC_ALL": True}}
CORE = ("hinata", "antilife", "creature_giving", "dragonstorm")   # decks the brief names; the beam's only users


def regime(arm, depth, bw, ld):
    """The (width, leafdepth, static) the hybrid resolves to -- a copy of the TurnSolver.cpp branch."""
    off = ("off",)
    if arm == "B":
        return off
    if arm == "C":
        return (20, 1, True)
    if depth == 5:
        return (bw, ld, False) if bw > 0 else off
    if depth == 3:
        return (20, 1, True)
    return off


def suite_maps(repo):
    """DECK_FILE / DECK_PROF and every case key of every tier, by sourcing the (data-only) suite matrix."""
    sh = r'''source test/regression_cases.sh
for k in "${!DECK_FILE[@]}"; do printf 'F\t%s\t%s\n' "$k" "${DECK_FILE[$k]}"; done
for k in "${!DECK_PROF[@]}"; do printf 'P\t%s\t%s\n' "$k" "${DECK_PROF[$k]}"; done
for c in "${SMOKE_CASES[@]}" "${REGRESSION_CASES[@]}" "${OVERNIGHT_CASES[@]}"; do set -- $c; printf 'C\t%s\n' "$1"; done'''
    out = subprocess.run(["bash", "-c", sh], cwd=repo, capture_output=True, text=True, check=True).stdout
    files, profs, cases = {}, {}, set()
    for ln in out.splitlines():
        p = ln.split("\t")
        if p[0] == "F": files[p[1]] = p[2]
        elif p[0] == "P": profs[p[1]] = p[2]
        elif p[0] == "C": cases.add(p[1])
    return files, profs, cases


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..")))
    ap.add_argument("--out", default="/workspaces/MagicDeckTester/build/scratch/beam-gate/ab")
    ap.add_argument("--base", type=int, default=6400001)
    ap.add_argument("--chunk", type=int, default=500)
    ap.add_argument("--core-games", type=int, default=2000)
    ap.add_argument("--core-2hg", type=int, default=1000)
    ap.add_argument("--core-pinned", type=int, default=400)
    ap.add_argument("--fleet-games", type=int, default=2000)
    ap.add_argument("--fleet-2hg", type=int, default=500)
    ap.add_argument("--fleet-pinned", type=int, default=200)
    ap.add_argument("--no-fleet", action="store_true", help="core decks only (the C arm then measures 4 decks)")
    a = ap.parse_args()
    repo = a.repo
    files, profs, cases = suite_maps(repo)
    cost = {}
    try:
        cost = {k: v.get("cost_per_game_ms", 0) for k, v in
                json.load(open(os.path.join(repo, "test/suite_cost.json")))["decks"].items()}
    except Exception:
        pass

    decks, seen = [], set()
    for key in sorted(files):
        if key.endswith("2hg") or key not in profs:
            continue
        prof = profs[key]
        stem = os.path.basename(prof)[: -len(".profile.json")]
        side = os.path.join(repo, os.path.dirname(prof), stem + ".value.json")
        if not os.path.exists(side):
            continue
        vp = json.load(open(side)).get("value_play") or {}
        if not (vp.get("enabled", False) and vp.get("target_depth", 0) > 0):
            continue   # no driving block => AIEngine passes beam_width -1 => no arm can beam
        if prof in seen:
            continue
        if key not in cases:
            continue   # not a suite deck (no GT, no row in any tier)
        seen.add(prof)
        core = key in CORE
        if a.no_fleet and not core:
            continue
        decks.append(dict(key=key, file=files[key], prof=prof, depth=vp["target_depth"],
                          budget=vp.get("budget_ms", 0), bw=vp.get("beam_width", 0), ld=vp.get("beam_leafdepth", 2),
                          has2hg=(key + "2hg") in cases, core=core))

    jobs, equiv, rows, k = [], {}, [], 0
    est_core_ms = {arm: 0.0 for arm in ARMS}

    def add(d, tag, depth, games, pinned, budget, hg):
        nonlocal k
        groups = {}
        for arm in ARMS:   # A first: the representative of a group is its first arm
            groups.setdefault(regime(arm, depth, d["bw"], d["ld"]), []).append(arm)
        reps = [g[0] for g in groups.values()]
        if len(reps) < 2:
            return   # every arm identical here: nothing to measure
        row = f'{d["key"]}{"2hg" if hg else ""}_{tag}'
        equiv[row] = {arm: g[0] for g in groups.values() for arm in g}
        rows.append(dict(row=row, deck=d["key"], depth=depth, budget=budget, games=games, run=reps,
                         regimes={g[0]: list(r) for r, g in groups.items()}))
        for c in range(0, games, a.chunk):
            n = min(a.chunk, games - c)
            seed = a.base + 1000 * k
            k += 1
            for arm in reps:
                j = {"name": f"{row}_{arm}_s{seed}", "deck": os.path.join(repo, d["file"]),
                     "profile": os.path.join(repo, d["prof"]), "games": n, "seed": seed, "game_index": 0,
                     "weight": int(round(cost.get(d["key"], 0)))}
                if pinned:
                    j.update({"depth": depth, "budget_ms": budget, "ignore_play_profile": True})
                if hg:
                    j.update({"starting_life": 30, "opponent_heads": 2})
                if ARMS[arm]:
                    j["flags"] = dict(ARMS[arm])
                jobs.append(j)
                est_core_ms[arm] += n * cost.get(d["key"], 0)

    # Play settings first (they are what matters), then 2HG play, then the pinned depths.
    for d in decks:
        add(d, "play", d["depth"], a.core_games if d["core"] else a.fleet_games, False, None, False)
    for d in decks:
        if d["has2hg"]:
            add(d, "play", d["depth"], a.core_2hg if d["core"] else a.fleet_2hg, False, None, True)
    for d in decks:
        n = a.core_pinned if d["core"] else a.fleet_pinned
        add(d, "d4", 4, n, True, d["budget"], False)
        add(d, "d3", 3, n, True, 10, False)

    os.makedirs(a.out, exist_ok=True)
    os.makedirs(os.path.join(a.out, "wins"), exist_ok=True)
    json.dump({"jobs": jobs}, open(os.path.join(a.out, "ab.json"), "w"), indent=1)
    json.dump({"arms": ARMS, "base": a.base, "rows": rows, "equiv": equiv,
               "decks": decks}, open(os.path.join(a.out, "rows.json"), "w"), indent=1)
    ng = sum(j["games"] for j in jobs)
    print(f"{len(decks)} decks ({sum(d['core'] for d in decks)} core), {len(rows)} rows, {len(jobs)} jobs, "
          f"{ng} games, seeds {a.base}..{a.base + 1000 * (k - 1) + a.chunk}")
    for d in decks:
        print(f'  {d["key"]:<16} play d{d["depth"]} b{d["budget"]} beam_width={d["bw"]} ld={d["ld"]}'
              f'{" 2hg" if d["has2hg"] else ""}{" CORE" if d["core"] else ""}')
    tot = sum(est_core_ms.values()) / 1000.0
    print("est core-s per arm (suite cost/game x games; suite cost includes mulligans):",
          {arm: round(v / 1000.0) for arm, v in est_core_ms.items()}, f"total ~{tot:.0f} core-s")
    print(f"manifest: {os.path.join(a.out, 'ab.json')}   rows/equivalences: {os.path.join(a.out, 'rows.json')}")


if __name__ == "__main__":
    main()
