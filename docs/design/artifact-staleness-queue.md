# Artifact staleness queue — regenerate the most out-of-date artifacts, in order

**Status: BUILT 2026-10-23, UNTESTED END-TO-END (USER re-opened it: "focus on making a script/application
that focuses on using CPU effectively when we don't have inference budget ... prioritize those that have
drifted the most first"). The tool is `scripts/artifact_queue.py` + `scripts/artifact_queue.json`; see
"2026-10-23: what was built" at the end. `status`/`explain` were exercised read-only; the worker loop
(`run`) has NOT been run yet -- the box had a mulligan generation on it, and the USER is freeing a second
machine to try it on. History below is kept as the record of the design.**

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

## 2026-10-23: what was built (`scripts/artifact_queue.py`)

```
python3 scripts/artifact_queue.py status [--all|--json]   # ranked artifacts + deck jobs; git reads only
python3 scripts/artifact_queue.py explain <deck>          # every axis for one deck
python3 scripts/artifact_queue.py mark <deck> mulligan|valueleaf "<why>" [--priority N]   # USER mark
python3 scripts/artifact_queue.py claims                  # what every machine is doing (origin/artifact-queue)
python3 scripts/artifact_queue.py start | stop            # detached worker loop; stop = after the CURRENT job
python3 scripts/artifact_queue.py run --dry-run           # rank + pick, claim nothing, run nothing
python3 scripts/artifact_queue.py selftest                # claim CAS on a local bare repo, .gz round-trip,
                                                          # parsing, ranking -- no games; run it on a new box first
```

**Trying it on a new machine:** a persistent checkout (the worker refuses a temp root), `gh`/git push
access to origin, then `selftest`, `status`, `run --dry-run`, and `start` once the box is known to be
free. `MDT_WORKER_ID` names the machine; otherwise an id is minted once per checkout into
`logs/artifact_queue/WORKER_ID` (NOT the hostname: a devcontainer's is the container id, which changes on
every rebuild and would orphan an in-flight job). Worker log: `logs/artifact_queue/<id>/worker.log`, with
per-deck driver logs beside it.
**Agents running, joining or watching a worker:** read `docs/design/artifact-queue-for-agents.md` (where to
look, the watch, what to leave alone).

**The drift metric is measured play churn, and it costs nothing.** The harness keeps one
`<game_index> <win_turn> <decision-stream digest>` line per suite game (`test/gt_logs/*.wins`). For each of
the deck's cases (standard and 2HG, every tier), the baseline is the version FIRST ACCEPTED AT OR AFTER the
artifact's adoption commit (the first GT that saw it live), and churn = the share of the deck's suite games
whose decision digest differs between that and HEAD -- and the SCORE is the mean per-game |win-turn change|
over the same games, because churn saturated on the first full run (50-99% for nearly every artifact:
Knights 92% churn at 0.010 turns vs Hinata2 97% at 1.09). The artifact's own effect is never counted;
a play change landed between adoption and that first accept is missed (an under-estimate, never an
over-one). Net dturn and churn are reported beside it. The adoption commit is the last
commit touching the keep table's `.gz`, and for a leaf the oldest commit in the newest unbroken run of the
same `eval_model` (so a `value_play` settings edit does not reset the leaf's age).

**Exact axes** (`FLAG_WEIGHT`), weighted only where GT drift cannot see them: decklist mismatch (1.0; a
list card no bucket names, or a bucketed card the list dropped -- an alias counts, it is lossy),
unreadable artifact (1.0), settings drift (0.1; table generated at a depth/budget other than the deck's
current `mull_gen_*`, only when the raw recorded one). Reported at weight 0 because their play effect is
already in the drift: cards.json change to a deck card since adoption (canonicalised exactly as
`CardDefHash`), pipeline order (leaf fitted before the current keep table was adopted). A USER mark sorts first
regardless of score, **until a run answers it**: any verdict the deck's ledger records for the marked kind
(adopted, kept-incumbent, or a trade held for the USER) from a run that started after the mark was set. `mark`
stamps the time to the second (`set`); an older date-only mark counts from the end of its day. An answered mark
is inert and `status` tags it `mark-answered(unmark)`. Before this, a mark outlived its answer: on 2026-10-28
the worker regenerated Angels again right after its marked table landed, then started a third Giants run for a
ruling answered on 10-25. An artifact is queued when score >= `min_drift` (0.05 turns), it is committed, and its deck
is in the suite (both drivers refuse otherwise). `min_drift` was 0.02 until 2026-10-30, when the USER raised it
to 0.05 on the first week's results: every keep table that came back >= 0.01 t better had started from >= 0.08
drift (Giants 0.079, Melira 0.41, Dragons 0.82, Goblins 0.86, Mirrorwing 1.14, Snow 1.23); runs from 0.00-0.04
(Angels x3) adopted tables only 0-0.003 t better, and all five leaf runs from 0.06-0.11 kept the incumbent.
At 0.02 the threshold sat at the 75th percentile of all 64 artifacts' drift, re-queueing decks for drift
too small to give a fresh artifact a real chance of adoption. A settings-only sidecar (no `eval_model`, e.g. Snow's
`leaf: none`) is not a leaf and is not queued unless marked.

**The queue unit is a DECK, and stages run mulligan -> valueleaf** (USER order 2026-10-18), so a leaf
regenerated in the same job is fitted to the table that job produced. `leaf_follows_mulligan` (default
false) would add the leaf stage whenever the keep table is regenerated -- see open question 2.

**Claims (multi-machine).** origin branch `artifact-queue`, one `claims/<deck>.json` per deck, written
with git plumbing (no working tree). Every write is a commit on the tip just read, pushed without force,
so a concurrent writer makes the push fail and the writer re-reads and retries: a compare-and-swap on the
whole queue. A running claim is renewed every 20 min and is stealable after 3 h without a heartbeat. A
finished claim holds while the deck's artifacts on the branch are still the blobs it ran against (i.e.
until its result lands) -- but only when it produced a result branch; one that adopted nothing (a leaf left
staged) is retryable like a rejected/refused one: once the deck's suite play has drifted `min_drift` turns
since the commit that attempt FROZE (below). A taken-over lease uses the same
rule as the picker, so the two cannot disagree. A MACHINE fault (`./build.sh` failing) releases the claim
and stops that worker instead of stamping every deck "failed" one after another, and three failed deck
jobs in a row also stop it.

**The worker.** Per machine (`MDT_WORKER_ID`, else the id minted into `logs/artifact_queue/WORKER_ID`): a worktree at
`.claude/worktrees/artifact-worker-<id>` (persistent location; build/ goes to the volume via build.sh),
re-frozen on `origin/<branch>` ONLY between jobs (tracked leftovers are committed to a pushed rescue branch
first, never discarded); `./build.sh`; then for each stage it waits until no foreign `mtg`/`mtg-analyze`
process is on the box, and runs `bash scripts/mullgen.sh run <deck> <recipe>` /
`bash scripts/valueleaf.sh run <deck>` -- the sanctioned entry points, unchanged. An interrupted job
resumes in the same frozen worktree from `logs/artifact_queue/<id>/inflight.json` (both drivers resume from
their own markers/journals). Whatever TRACKED change the drivers leave in the deck folder (an adopted
table re-packed to `.gz`, an adopted sidecar, a screened `mull_gen_*`) is committed to
`artifact-refresh/<deck>-<date>` and pushed; gitignored leftovers (a quarantined `.DISABLED` table, `.PREV`,
journals and their backups) stay in the worker's worktree, which is on the persistent host drive. **Landing
is a human/agent step**: rebase it onto the branch, run smoke + regression, accept the GT it moves.
Nothing is ever killed: `stop` takes effect between jobs.

**A regeneration runs the incumbent's recipe, `fast` or `complete` only** (USER 2026-10-23: *"Let's try to
have them reasonably replace the existing one. If they use much lower settings they are all but guaranteed to
fail."*; refined 2026-10-25: *"If the original ran complete we should at least run that. Though to be fair there
are some that ran more than complete. In that case we can just leave it be"*, and *"I'm okay with just
considering complete and fast for this pipeline."*). `mull_plan` reads the incumbent's R (profile `effective_R`,
raw `meta.R`): R<=30 -> `fast`, R<=40 or unrecorded -> `complete`. A table above 40 (the burn/slivers/knights-era
R=60 tables) is beyond `complete`: it queues on drift like any other and regenerates at `complete`, and the
usual old-vs-new A/B keeps the incumbent unless the fresh table is better (USER 2026-10-30: *"we shouldn't need
a special mark. It should just be if it has drifted enough"*; until then such a table needed a USER mark). Depth and budget are the deck's screened `mull_gen_*`, exactly what `mullgen.sh` picks; an
incumbent's own generation depth is reported by `explain` but not imposed. (The 2026-10-23 build also raised
depth/budget to the incumbent's and pinned `MTG_KEEP_ROLLOUTS` above 40; both were dropped 2026-10-25.
Anti-Lifegain's 2026-07 table, for example, was rolled at the then-default d5 b20, recorded nowhere in the
artifact, and its regeneration ran at the screened d1 b3 -- same recipe, so its kept-incumbent verdict stands.)
`recipe_overrides` (USER) still wins. The plan is pinned in `inflight.json` so a resume cannot change R under a
banked journal. The value leaf needs nothing: `valueleaf.sh` has one fixed route.

**A failed replacement is a VERDICT and marks the drift as addressed** (USER, same day: *"if a new table
or value-leaf doesn't improve quality we still don't want to do it again"*). Every stage outcome that is a
verdict -- `adopted`, `adopted-ungated`, `kept-incumbent` (mullgen's old-vs-new rejected the new table, or
valueleaf.sh did not adopt its leaf) -- is appended to `test/artifact_ledger/<deck>.json` (one file per
deck, so result branches never conflict) with the run id, the artifact identity (keep table: its `.gz`
blob; leaf: its `eval_model` hash, so a `value_play` edit does not reset it), the frozen commit, the
settings and the A/B delta, and it rides the result branch. A **verdict-only** branch changes no artifact,
so it moves no play and needs no GT accept: land it as-is. From then on that artifact's drift is measured
from the LATER of its adoption and its last verification, so it re-enters the queue only after play has
drifted again since the attempt, by the same `min_drift` that admits a deck in the first place.
Before the branch lands, its claim (`rejected`/`done` with a result branch) blocks a redo indefinitely; once
landed (the claim's run id is in the ledger) the ledger governs.

**A rejected or refused attempt waits for drift, never for the calendar (USER 2026-10-30).** *"If we reject
or refuse it should wait until the drift has widened from the new version that we built from (at least some
amount)"*, *"since the last version we attempted ... Not away from the current profile we have adopted"*, at the
same bar as first entry (a retry has already drifted at least that far since adoption: *"them being the same
is probably fine for our purposes"*), and the purpose: *"We want to have at least a decent chance that the additional drift would make
the newly created resources adoptable."* A re-run on unchanged play only repeats the verdict. So a
`rejected`/`refused` claim (or a `done` one that adopted nothing) with no result to land holds until
`note_claim_drift()` measures >= `min_drift` mean per-game |dturn| on the deck's suite GT since the
claim's `frozen` commit; unmeasured (no suite cases) holds. It replaced a flat 14-day retry, which would have
re-run Dragonstorm/AntiLifegain/ColossusHammer on 2026-11-08 at ~0 drift. Only `failed` (a crash, no verdict
on the deck; a tool fix moves no play) still retries after `retry_rejected_days` (14).

**A `mullgen.sh` regeneration trap the worker works around (found while building this).** The driver
decides REGENERATION vs FIRST VERSION with `[ -e <stem>.keepmodel.exhaustive.profile.json ]`, the PLAIN
name, and every deck commits only the `.gz`. So `mullgen.sh run` on a committed deck (1) saves no
incumbent, which makes it validate with the first-version bar (keep vs static, confounded bottoming)
instead of the old-vs-new versus A/B, and (2) leaves the new plain table SHADOWED by the old `.gz`, which
the engine loads first (`MulliganProfileIO.h`, `.gz` before plain). The worker unpacks the incumbent to
the plain names in its own worktree before the driver runs, and re-packs afterwards (byte-identical
restore when unchanged). The driver itself was NOT edited (a generation was running on the box); the
proper fix is in `mullgen.sh` line ~658 (resolve the `.gz` incumbent the way `validate_preflight` already
does) and in whatever made the new plain file live. Anyone who has hand-run a regen recently should check
which validation it ran.

**Open questions (USER):**
1. ~~recipe fast vs match the incumbent~~ -- ANSWERED 2026-10-23: never below the incumbent (above).
   Cost consequence: most shipped tables are R=40, so most regenerations are `complete`, and the R=60 ones
   are dearer still; the queue does not project wall-clock before claiming.
2. Should a keep-table regeneration always be followed by that deck's leaf (`leaf_follows_mulligan`)? It
   is exactly the pipeline-order axis, but it costs a leaf run per table, and the leaf is currently the
   weaker evaluator (USER 2026-10-18).
3. Weights (`FLAG_WEIGHT`, `kind_weight`, `min_drift`) are first guesses; the drift score alone is the measured part.
4. Idle detection sees this container's processes plus the 1-minute load average (`idle_max_load`, default
   half the cores; VM-wide under WSL2, so it usually catches a sibling container's generation, but a
   sibling's single-threaded phase can slip under it): on a shared box, start the worker only when the box
   is known to be free.

## 2026-10-25: auto-land clear wins and clear losses (USER) -- BUILT 2026-10-26 as `land_result`, see the guide

The USER: *"It seems our tool is missing a way auto-adopt wins and auto-reject losses? Only ones that are
in-between should be left up to the user."* Today every result stops at a branch
(`artifact-refresh/<deck>-<date>`) and waits for an agent to rebase it, run smoke + regression and accept GT.
Deferred on 2026-10-25 only because inference budget was nearly out (the USER: fine for the worker to just keep
generating for the next 24 h); the results piling up meanwhile are StompySurprise, Mirrorwing, Goblins, Snow
(play-moving) and Anti-Lifegain, Colossus Hammer `-1418` (verdict-only; Colossus `-0245` must never land).

What to build, so that only the in-between needs the USER:
- **Three classes from the driver's own A/B**, decided by the worker and written to the ledger:
  - *clear win*: new better than old beyond the tie band (`mullgen.sh` versus pool; the leaf's phase-E
    gate, which already requires cheaper-and-not-notably-worse) -> auto-land.
  - *clear loss*: new worse beyond the band -> auto-land the verdict-only branch (incumbent kept, drift reset).
  - *in the band*: apply the USER's 2026-10-24 tie rule first (slightly better within the margin -> keep the
    new; slightly worse -> reject, or grow the test, as was done for StompySurprise at 12 rounds). Only what that
    rule still cannot settle is left for the USER, listed in the report.
  - A leaf whose candidate is a *trade* (cheaper on quality, dearer on cost or vice versa) is never a clear win:
    the USER does not want cost added to the value leaf (2026-10-24).
- **Landing is a box-wide step, not a per-job one.** Smoke + regression + GT accept cannot run beside a
  generation (strictly serial), so the worker lands in a batch between jobs: rebase every pending clear branch
  onto `phase-1-2-deck-analyzer`, ONE smoke + ONE regression over all of them, accept, push. A red tier
  (crash, scenario failure, byte-identity break) stops the landing and leaves the branches for an agent; a GT
  move is the expected outcome, not a failure. Verdict-only branches land with no tier at all.
- **One landing machine.** Several workers pushing to the deck branch would race on GT; take a
  `claims/_landing.json` lease (same CAS) so only one box lands at a time.
- No USER sign-off is owed for an adoption that is better overall -- average quality better and performance not
  worse (USER 2026-10-26). Report the numbers as done; only a trade-off goes to the USER. Keep tables and value leaves
  alike; for a leaf the performance half matters more.
