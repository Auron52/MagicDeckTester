# Artifact staleness queue — regenerate the most out-of-date artifacts, in order

**Status: PARKED (USER 2026-09-03; re-raised 2026-10-10, still parked -- see the update at the end).
The USER's idea, recorded verbatim in spirit; parked purely for CPU cost, not for design doubts. Do not
build without the USER re-opening it.**

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

## 2026-10-10 update: re-raised, still parked -- ordering by drift, a manual mark, and a fourth axis

The USER, 2026-10-10: *"I would like to, at some point, have a list of most stale artifacts and have a
regeneration queue for them that machines can pull from when they have nothing else to do. This
requires more computers, though."* -- then *"For now, I want to focus on adding new lists."* New
relative to the 2026-09-03 sketch: **several machines pulling from one queue when idle** (not one
machine), and on ordering:

> *"For the queue we would probably order tasks by staleness, which would mean drift and likely also
> want a way to manually mark certain artifacts as especially stale. This would be a case where I would
> probably do that."* (about Snow's keep table, below)

* **Order by drift** (the sketch's staleness metric), plus **a manual "especially stale" mark set by
  the USER** that puts an artifact at the front regardless of measured drift and carries a reason (like
  `keep_table_alias`'s `why`). Where the mark lives -- beside the artifact or in one queue file -- is
  open.
* **A staleness axis the sketch did not have: DECKLIST MISMATCH** -- exact and free to check. The
  artifact names cards the list no longer has, or the list has cards the artifact cannot see; for a keep
  table every hand holding such a card misses the table entirely. Checked 2026-10-10 by comparing each
  shipped table's `exhaustive_keep.buckets` with the main deck of its `.cod`/`.txt`:
  * **Snow -- MISMATCH.** The table (generated 2026-09-25 at `880cb9b1`) buckets the MDFC as Kaldring,
    the Rimestaff; the list has played it as its front face Jorn, God of Winter since 2026-10-03
    (`9dc0a9b2`, USER). Every Jorn hand fell back to the clairvoyant trial-game bottomer: 60-64% of ALL
    of Snow's searched overnight work, and every game on the suite's Snow slow-game list (seed 5100 gi95:
    a mulligan to 4 with Jorn, 96% of its 21.6M units in trial games). Stop-gap (USER: *"point Jorn at
    Kaldring for the time being"*): `mulligan.keep_table_alias` in `decks/Snow/Snow.profile.json` --
    lossy (the bucket was learned for Kaldring's play); Snow overnight held-out searched +41 turns / 1000
    games, searched units 198M -> 57M. **Snow's keep table is the first artifact the USER would mark.**
  * Every other shipped table matched its list. (StompySurprise keeps a stale, gitignored, uncompressed
    `.json` from its pre-2026-09-17 list beside the shipped `.gz`; the engine loads the `.gz` first.)
  * The engine now prints `[keeptable] WARNING: hand card "X" has NO BUCKET` the first time a hand misses
    its table this way, so this axis is no longer silent.
* Two more axes worth scoring: **pipeline-order drift** (a keep table fitted before the deck had a value
  leaf, e.g. Snow's) and **settings drift** (`value_play` generation keys or bucket rulings changed since
  generation). Example of plain play drift: Bruna's value leaf predates the 2026-10-10 width collapses.
* Still open: the claim/lock protocol between machines, where the queue lives, and whether an idle box
  may regenerate while it also runs suite tiers (contention makes timing-based outputs wrong).
