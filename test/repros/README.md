# Committed repro manifests

Minimal, self-contained `mtg --batch` manifests for defects under active investigation. Everything
a manifest here references is TRACKED, so it reproduces on a fresh clone — unlike the working copies
under `logs/`, which are gitignored and therefore machine-local.

Rules for anything added here:

- reference only tracked paths (decks, profiles, committed sidecars) — never `logs/**`;
- keep it to ONE game where possible, so the repro is a seconds-to-minutes check, not a sweep;
- record the expected output in this file, so a reader can tell PASS from FAIL without the
  conversation that produced it.

---

## `mirrorwing_h5_gi14.manifest.json` — search/play mismatch

Full analysis: `docs/design/mirrorwing-search-play-mismatch.md`.

```bash
./build.sh
MTG_TRACE=search ./build/Release/mtg --batch test/repros/mirrorwing_h5_gi14.manifest.json --threads 1
```

Runs one game (seed 8022, `game_index` 14) at depth 5, unbounded budget, `max_turns` 8 — the matrix
H5 cell. Takes ~25 s single-threaded.

**Current (DEFECTIVE) output**, first decision:

```
[search] T1 pass=1 done win=6 cost=4
[search] T1 pass=2 done win=5 cost=38          <-- correct answer, at 38 units
[search] T1 pass=3 done win=6 cost=636
[search] T1 pass=4 done win=7 cost=6613
[search] T1 pass=5 done win=8 cost=1332428     <-- COMMITTED: wrong by 3 turns, 35,000x the cost
H5_gi14_repro: played=1 avg=5.0000 digest=3e487031ad353250
```

The game WINS ON T5 (`avg=5.0000`), and `MTG_DUMP_EWINS` confirms `earliest=5` on the true library
order — with the top candidate being the very line the search commits (`land Forest`, cast
`Ignoble Hierarch`). So the search picks the RIGHT play and values it at 8 instead of 5.

**ACCEPTANCE TEST: `T1 pass=5` must report `win=5`.**

This is a sharper test than any aggregate because breadth is already excluded as a confound — three
independent levers were measured and none recovers the win (`MTG_NO_GROUP_CAP=1` is byte-identical,
`MTG_UNPRUNED=1` makes it WORSE at `win=9`, and `MTG_BP_SEARCH=16 MTG_BP_DEPTH=4` costs 1.7x for no
change). A completeness fix therefore cannot be the answer, and if a change makes `pass=5` report 5,
it did so by changing the MODEL, which is the actual defect.

Note the ladder commits the deepest completed pass unconditionally
(`TurnSolver.cpp` ~16535: `line = attempt;` with no comparison), so `pass=5`'s answer is what play
receives — `pass=2` having been right does not help.

**Do not** "fix" this by making the ladder keep the best pass. That hides the defect rather than
removing it; the deeper pass must stop being wrong.

The `value_profile` here points at the committed `decks/Mirrorwing Dragon/Mirrorwing Dragon.value.json`,
whose `eval_model` is byte-identical (sha256 `b1339b0eaafaaa76`) to the `logs/eval/...STAGED.json`
the original manifest referenced — verified to reproduce the numbers above exactly.
