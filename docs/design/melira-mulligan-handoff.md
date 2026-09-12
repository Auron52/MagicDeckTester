# Melira Pod — exhaustive mulligan profile: run record + machine handoff

Launched 2026-09-12 on the primary box (32 cores). This document is the **standalone runbook for
moving the run to a second machine** if it has not finished in the user's window (~70 h). It lives in
`docs/design/` on purpose: `logs/` and the generation artifacts are gitignored, so a handoff note
written there would never reach the other machine. Everything needed to resume is below.

Read alongside `.claude/skills/mulligan-profile.md` (the authoritative skill); this file only pins
*this* run's parameters and the transfer mechanics.

## The freeze (all of this must match on the second machine)

| thing | value |
|---|---|
| commit | `273fa40d35a5eb2b4d808832edc05ded73bb39c0` (`273fa40d`), clean tree |
| `HEAD:src` tree | `5b5f3201bfd18d90a67357ba30d341ee7532c39f` |
| **play digest** | `60c687d646c785da`  (d3/b3, 64-game battery) |
| recipe | **`fast`** — adaptive bottoming, cap R30 (user's choice, 2026-09-12) |
| rollout depth / budget | `3` / `3 ms` — from `value_play.mull_gen_depth` / `mull_gen_budget_ms` |
| discovery | 400 probes, depth 5, budget 20 ms, `equiv_seed` 20260701, `--max-turns 8` |
| buckets | K=23, pinned by the **user ruling** in `decks/Melira Pod/Melira Pod.buckets.json` |
| `seed_base` (primary) | **`1000000`** (the recipe default) |
| `seed_base` (secondary) | **`2000000`** — only for Route B; Route A reuses `1000000` |

The **play digest is what resume and pooling actually gate on**, not the commit string. Two builds
whose digest matches are interchangeable for this run.

### Why this is a fresh generation and not a resume of the September 9 run

There is a prior 194,406-line journal from `0f75acea` at R40 (`complete`), preserved under
`logs/melira_mullgen_prior/` (`STALE.journal`, plus `journal.0f75acea.R40.json.gz`). Its play digest
is `43c5f36baafc3fc7`. HEAD's is `60c687d646c785da` — the search-shape work that landed between
Sep 9 and Sep 12 moved Melira's play at gen settings. That journal is therefore **neither resumable
nor poolable** at this commit. It has been moved out of the deck folder, **not deleted**, and should
stay that way until the user rules on it.

## Files, and where each one comes from

Tracked in git — the second machine gets these from `git checkout 273fa40d`:

```
decks/Melira Pod/Melira Pod.cod
decks/Melira Pod/Melira Pod.buckets.json        # the K=23 user ruling -- required for bucket_fp parity
decks/Melira Pod/Melira Pod.profile.json        # static/base profile
decks/Melira Pod/Melira Pod.value.json          # value leaf; supplies mull_gen depth/budget
```

**Gitignored — must be copied out of band** (scp/rsync/USB), and these are the run's actual state:

```
decks/Melira Pod/Melira Pod.keepmodel.exhaustive.raw.json.journal   # every committed cell (THE resume state)
decks/Melira Pod/Melira Pod.keepmodel.exhaustive.raw.json           # written when the gen completes
decks/Melira Pod/Melira Pod.keepmodel.gencache.json                 # cached buckets (saves ~6 min re-discovery)
decks/Melira Pod/Melira Pod.keepmodel.exhaustive.raw.json.probe     # the scout's r=0 slice
```

## Route A — CONTINUE the same run on the other machine (use this if it is simply unfinished)

This is the right route for "70 h elapsed and it is at 60%". Resume is journal replay: completed
cells are skipped and nothing is repaid. The rollout seed is a pure function of
`(seed_base, r, w, pd)`, so continuing on another machine is byte-identical work.

```bash
# --- on the SECOND machine ---
git clone <repo> && cd MagicDeckTester
git checkout 273fa40d
./build.sh                                  # NEVER bare cmake -- see CLAUDE.md

# copy the four gitignored files above into decks/Melira Pod/ , then:
bash scripts/mullgen.sh run "decks/Melira Pod" fast
```

That is the whole procedure — **the identical command is the resume command**; there is no resume
flag. Confirm at startup that it prints:

```
rollout-config play digest (d3/b3, 64-game battery): 60c687d646c785da
  seed            : 1000000
```

If the digest differs, the second machine is not on the frozen commit (or has a dirty tree) and the
resume will be **refused loudly** rather than silently mixing two engines' rollouts — that refusal is
working as intended, not a bug to route around.

**Stop the primary before, or instead of, continuing there.** Route A is a move, not a fork: two
machines advancing the same `seed_base` produce overlapping rollout streams that the merge will
reject as double-counting.

## Route B — ADD rollouts in parallel, then pool (use this to buy precision, not to finish)

Two `fast` chunks at disjoint seeds pool to **effective R=60** — above `complete`'s R40 — for less
wall clock than one `complete` run. Use this when the primary run has *completed* and you want a
sharper table (bottoming is the R-sensitive half; keep is already robust at R30).

```bash
# --- SECOND machine: same commit, same recipe, DIFFERENT seed ---
build/Release/mtg-analyze "decks/Melira Pod/Melira Pod.cod" \
    --cards-json src/cards/data/cards.json --gen-mulligan fast --seed 2000000

# --- back on the primary: pool the two raws ---
MTG_KEEP_MERGE=1 \
  MTG_MERGE_INPUTS="decks/Melira Pod/Melira Pod.keepmodel.exhaustive.raw.json,/path/to/secondary.raw.json" \
  build/Release/mtg-analyze "decks/Melira Pod/Melira Pod.cod" --cards-json src/cards/data/cards.json
```

Expect `pooled 2 file(s); 2 distinct seed_base(s); effective R=60`. A killed run's `.journal` is
itself a valid merge input, so partial work is never stranded either way.

**Do the determinism handshake once** before trusting a cross-machine pool: both machines run an
identical tiny config (same `--seed`, `MTG_KEEP_ROLLOUTS=2`, and **`MTG_KEEP_REFS_OFFSET=0`** — at
the shipped default of 2 the freeze-shrink target is re-derived on a *timing-triggered* schedule, so
a borderline cell can stop at a different R between two runs of the same binary and seed, which
would fail the handshake for a reason unrelated to machine parity). Confirm identical `bucket_fp`
and `deck_fp` and byte-identical V, then discard those and run the real distinct-seed chunks.

## Validation is not optional and is already wired in

`scripts/mullgen.sh run` generates **and** validates as one operation — the profile is
presence-gated, so the file existing IS adoption, and a generation that landed without games played
against it has happened before. Whichever machine finishes the gen runs, automatically:

1. **keep** — exhaustive vs static, bottoming held identical (16 seeds × 1000 games/round);
2. **bottoming** — blind exhaustive vs lookahead under `MTG_CONFOUND_BOTTOM=1` (the naive A/B is
   confounded: it scores lookahead on the very library it peeked at);
3. the **regression suite**, for visibility only — it can never reject (the standard goldfish metric
   is unconfounded, so it still rewards lookahead's peek).

Bar: *"at all worse on average"*, no significance margin; a tie escalates to fresh disjoint seeds.
On failure the profile is quarantined to `...profile.DISABLED.json`.

**Watch item:** the confounded bottoming gate now fails decisively on Dragons and Mirrorwing v3 for
reasons still unexplained — read `docs/design/confounded-bottoming-gate-failures.md` before
re-deriving a hypothesis if Melira fails it too. There is no ship-bottoming-off escape hatch; the
response is raise R or fix the heuristic.

## Measured size and throughput (from the launch scout, 2026-09-12 ~03:40 UTC)

```
K check OK: K=23 matches value_play.expected_buckets
distinct hands: size7=788081 size6=228576 size5=57385 size4=12118 size3=2065 size2=266 size1=23
                (total 1088514)
continuous size-7: 788081 cells (= 1,576,162 cell-sides) + 600,866 fused sub-table batches
observed rate    : ~98 rollouts/s on 32 threads  (~3/s/core)
```

That rate is far below the skill's `~110/s/core` rule of thumb, because Melira's rollouts are
genuinely expensive at d3/b3 — so **do not size this run off the rule of thumb**. The r=0 floor
slice alone (1,576,162 cell-sides at 98/s) is ~4.5 h. Whether `fast` fits a 70 h window depends
entirely on how aggressively the adaptive keep schedule freezes cells: a mean final R of ~8 lands
near 36 h, ~15 lands near 67 h, and no freezing at all (R30 flat) would be ~134 h plus sub-tables.

**The authoritative number is the scout's own `GEN-TIME PROJECTION`**, which prints when the scout
finishes (~4.5 h after launch) at the end of `logs/Melira Pod_mullgen/scout.log`:

```bash
grep -A4 "GEN-TIME PROJECTION" "logs/Melira Pod_mullgen/scout.log"
```

Read that before deciding whether to pre-arrange the handoff. The scout's work is **not** overhead:
its r=0 slice is carried into the real gen byte-identically (the probe-carry gate checks
fingerprints, `play_digest`, `seed_base` and depth/budget — notably **not** cap R, so a probe taken
in a cap-40 context is still reused by this `fast`/R30 gen).

### Watch item: a degenerate combo cell

The scout logged a **38.8 s** single rollout on `Melira x2 + Kitchen Finks + Forest + Chord of
Calling x3` — the persist/`-1/-1`-prevention loop. That is the deck's known infinite engine, and
cells containing it are orders of magnitude slower than the mean. If the run comes in far over
projection, this is the first place to look; `...raw.json.slow.log` carries the reproducing seed for
every rollout ≥ 30 s.

## Health checks while it runs

- `bash scripts/mullgen.sh status "decks/Melira Pod"` — where it is up to.
- The batch heartbeat should show near-full worker occupancy. At launch the primary measured
  **~31.4 of 32 cores** (3143% CPU, load 31.5). If that number is not near N/N, fix the scheduling
  before looking for an engine explanation.
- Slow rollouts (≥30 s) stream to `decks/Melira Pod/Melira Pod.keepmodel.exhaustive.raw.json.slow.log`
  with the seed that reproduces them. This cannot be disabled.
- Logs: `logs/Melira Pod_mullgen/` — `scout.log` (projection), `gen.log`, `chain.log`,
  `VALIDATION.txt`. A frozen copy of the generating binary is at
  `logs/Melira Pod_mullgen/mtg-analyze.frozen`.

## Open question for the user (not blocking — the run is going either way)

The Sep 9 R40 journal is dead at this commit. It is preserved, not deleted. Worth ~9.5 MB and 194k
rollouts of nothing at HEAD; say the word and it gets removed, otherwise it stays under
`logs/melira_mullgen_prior/`.
