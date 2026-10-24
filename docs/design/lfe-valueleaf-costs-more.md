# LandfallElementals value leaf: every candidate costs MORE than heuristic rollouts (open, for 2026-10-26)

**Status: parked until Monday 2026-10-26 (USER).** The USER does not accept "leave it on heuristic
rollouts" as the conclusion: *"To me this points to a bug or other implementation problems."*

## What the run measured

`bash scripts/valueleaf.sh run decks/LandfallElementals`, frozen at 57a46e33c (src 841e1e41), finished
2026-10-24 16:39 UTC, rc=0. Queue: `logs/vlq_landfallelementals/` (DECISIONS.txt, leafcal.json).
Phase E A/B, 8 seeds x 1000 games per arm against live (no sidecar, heuristic rollouts):

| arm | delta avg turn | t | cost (units vs live) | core-s vs live |
|---|---|---|---|---|
| staged | -0.0423 | -15.4 | 1.555x | 1.82x |
| nlesc | -0.0239 | -12.6 | 1.327x | 1.45x |
| nlfit | -0.0134 | -4.6 | 1.283x | 1.35x |
| emul | -0.0093 | -4.0 | 1.373x | 1.28x |

All four are better on every seed and all four cost more, so E.5 adopted nothing ("a TRADE, the
user's call"). E.7 and E.6 were skipped because no leaf is live.

## Why it looks like a defect rather than a trade

* A value leaf replaces the horizon rollout with an O(1) evaluator. On every other deck the leaf
  shapes are cheaper than, or close to, live. A leaf that costs 1.3-1.6x MORE in deterministic units
  means the search is doing more work around it, not less.
* Phase C0 leaf calibration logged `H5 paired 50 moved 0 ... units 1.000x NOT ENGAGED (identical units
  -- stale engine?)`: at H5 the greedy and 1-ply leaves produced byte-identical work, so the setting
  under test never took effect at that depth.
* Even the leafless shapes (nlesc, nlfit) cost more than live, so the extra cost may sit in a code
  path shared by every non-live arm (sidecar presence switching on the H-cell ladder, or escalation)
  rather than in the evaluator itself.

## First things to check

1. Where the extra units go: a census or unit-site split of staged vs live on a few paired games.
   (Note: `scripts/turn_census.py` currently refuses its own output -- the `u_*` buckets do not sum
   to `units` on 3162/3240 rows, so a Consume site is missing a bucket; fix that first.)
2. Why H5 is NOT ENGAGED in calibration on this engine.
3. Whether the sidecar's presence arms something Landfall-specific (land-drop / fetch breakpoints,
   Reef lines) that live never pays for.

Phase A context: many Landfall row games ran 1-4 h each (one hit the 14,400 s cap), which is a
separate cost problem worth checking with the same census.
