# Artifact staleness queue — regenerate the most out-of-date artifacts, in order

**Status: PARKED (USER 2026-09-03). The USER's idea, recorded verbatim in spirit; parked purely
for CPU cost, not for design doubts. Do not build without the USER re-opening it.**

## The idea (USER)

A queue that can be run on one machine and updates the most out-of-date artifacts in order of
staleness. "I may eventually set up an approach to handle stale artifacts... this has very high
CPU requirements, so for the time being it is parked."

## Why it exists

Per-deck artifacts (value leaves, exhaustive keep/bottom profiles) are engine-state fingerprints:
they are fitted to the play of the commit that generated them, and every adopted play-logic
change (most recently the tight sound recipe, `ebfb5f74`, which shifted hinata play by
~−0.006..−0.010) leaves them slightly stale. Today staleness is handled ad hoc — a deck gets
regenerated when someone notices or when a bug forces it (the 2026-09-02 Dragons/Mirrorwing
feed_sub repairs). A queue makes it systematic without demanding a full regen after every
adoption.

## Sketch (to be designed properly when un-parked)

* **Staleness metric:** per artifact, the distance between its recorded generation fingerprint
  (`commit` / `HEAD:src` tree hash in the sidecar) and current HEAD — refined by whether the
  intervening commits touched play logic at all (docs-only commits do not stale anything), and
  ideally by a measured play-drift signal (e.g. the deck's GT digest churn since generation).
* **Ordering:** most stale first; the USER's decks of active interest could carry a priority
  boost.
* **Execution:** one machine, one artifact at a time, honoring the STRICTLY SERIAL generation
  pipeline (profile → value leaf → mulligan, each alone on the box at its real settings —
  CLAUDE.md rule). The queue is exactly a serialization mechanism, so it composes with that rule
  instead of fighting it.
* **Freeze discipline:** each regen freezes on the HEAD it starts from (artifacts are
  commit-bound); the queue records the freeze so a mid-run adoption elsewhere does not
  invalidate the in-flight item.

## Constraints already known

* Very high CPU: a full value-leaf + mulligan regen is hours-to-days per deck.
* `valueleaf.sh run` / `mullgen.sh run` are the only sanctioned entry points (no hand-rolled
  phases).
* Bottoming is always on; sidecar PRESENCE is adoption — staged artifacts must not land beside
  decklists until accepted.

## Context

`docs/design/per-deck-folder-layout.md`, `.claude/skills/value-leaf.md`,
`.claude/skills/mulligan-profile.md`, the 2026-09-02 tight-recipe adoption in
`docs/design/bp-greedy-continuation-deletion.md` (the play change that most recently staled
hinata's artifacts).
