# Sunday Merfolk Turns and Boars artifact snapshot (2026-11-16)

Branch: `wip/sunday-artifacts-2026-11-16`, based on `245c26f2a`. This branch
shares generated artifacts for the ladder-shape agent. It is not an adoption:
the regression ground truth has not been accepted on this branch.

## Merfolk Turns

- Generated from frozen commit `92993e95b` with PGO+LTO. The fast exhaustive
  keep and bottom profile passed the generator's A/B validation. The committed
  forms here are `Merfolk Turns.keepmodel.exhaustive.{profile,raw}.json.gz`,
  alongside the screened `Merfolk Turns.profile.json` (17 buckets, d1/b1
  generation setting).
- Value-leaf regeneration completed. No candidate cleared the adoption rule;
  no value sidecar is included. Against the heuristic ladder over 8,000 paired
  games, NLESC moved 16 games faster and 19 slower (mean +0.000375 turn), at
  1.027x deterministic search units and 0.93x measured core time. NLFIT moved
  17 faster and 28 slower (mean +0.001375 turn), at 1.001x units and 0.87x
  measured core time. The adoption rule uses units, because core time can move
  with host contention. These results motivate a direct comparison with the
  rollout-skipping ladder prototypes when those are ready.
- Full local logs and staged shape sidecars remain in the generator worktree
  `.claude/worktrees/sunday-artifacts/logs/vlq_merfolk_turns/`.

## Boars

- Generated from frozen commit `245c26f2a` with PGO+LTO. Its fast exhaustive
  keep and bottom profile passed the generator's A/B validation. The committed
  forms here are `Boars.keepmodel.exhaustive.{profile,raw}.json.gz`, alongside
  the screened `Boars.profile.json` (18 buckets, d1/b1 generation setting).
- Value-leaf phase A banked 8,365 labelled rows before the 24-worker process
  reached its 40 GB RSS guard. It resumed incrementally at 12 workers on
  2026-11-16; a watchdog will retry at 8 and 4 if needed. No Boars value
  sidecar is included yet. The live state is recorded in the main checkout's
  `logs/sunday_artifacts/boars.state`.

Both generated profiles passed validation in their frozen worktrees. Shipping
them on the main branch still requires adoption attribution, ground-truth
acceptance, and a regression pass there.
