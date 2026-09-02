# Is this mulligan generation sampling its bottoming sub-tables? — how to tell, mid-run

**Read this if you are running, or resuming, an exhaustive mulligan (keep/bottom) generation —
especially a long one on a slow machine.** A generator bug that shipped two bad tables (Dragons
2026-08-30, Mirrorwing 2026-09-01) could waste an entire multi-day run while every progress line
looks healthy and the run exits *successfully*. Checking costs one read-only command and does not
touch the job.

This note is self-contained: you do not need any other context to use it.

---

## 1. The one-line check

```bash
python3 scripts/check_keep_subtables.py <path>
```

`<path>` may be any of the three, in any mix — it auto-detects:

| what you have | pass this | when |
|---|---|---|
| the generation's stdout/stderr log | `.../gen.log` | **best** — works from ~10 min in |
| the live journal | `<deck>.keepmodel.exhaustive.raw.json.journal` | any time after cells start landing |
| a finished run | `<deck>.keepmodel.exhaustive.raw.json[.gz]` | after the run ends |

Exit `1` = starved (act on it), `0` = healthy or inconclusive. It is read-only.

**If you have no log** (e.g. output went to a terminal that is gone), use the journal — it sits next
to the deck's raw sidecar and is written continuously.

### No script on that machine? Two greps do it

The script may postdate the checkout on the machine running the job. **You do not need it**, and you
do not need to pull — everything below reads files the run is already writing:

```bash
# 1. Is the sub-table counter moving? (the whole test)
grep -o "rollsub=[0-9]* .*sub=[0-9]*/[0-9]*" <gen.log> | tail -3

# 2. Did this run skip the floor phase? (the trigger)
grep -E "RESUME\(journal\)|resuming refine|floor complete" <gen.log> | head
```

Grep 1 is the verdict: **`sub=0/<big number>` and `rollsub=0` on the latest lines, with the run well
underway, means starved.** Grep 2 says why: `resuming refine` without a preceding `floor complete` is
the failing path.

*(Pulling the repo is safe for a running generation — it rewrites source files, not the loaded binary
or the journal. But you only need to pull if you intend to rebuild, per §5.)*

---

## 2. What the failure looks like, so you can read it yourself

The generator counts sub-table work separately from size-7 work. Every monitor line (printed every
300 s) carries both:

```
[keepgen]   monitor: 300s  phase=refine  roll7=120414 (401/s)  rollsub=0 (0/s)  sub=0/142464 (0.0%, 0.0/s)
                           ^^^^^^^^^^^^                        ^^^^^^^^^^^^^^   ^^^^^^^^^^^^^
                           refine from the very first line     never any        never moves
```

**`sub=N/M` is sub-table batches done / total. If `N` stays at 0 while the run makes progress, the
bottoming half is being skipped.** `rollsub=0` says the same thing in rollouts. Both are printed by
default and cannot be turned off.

The clearest way to see it is late in a starved run, where the two counters diverge completely
(real output from the Mirrorwing failure):

```
rollsub=0 (0/s)  frozen=265252/311956 (85.0%)  sub=0/142464
rollsub=0 (0/s)  frozen=284776/311956 (91.2%)  sub=0/142464
rollsub=0 (0/s)  frozen=305563/311956 (97.9%)  sub=0/142464
```

**97.9% of the size-7 work done, and not one sub-table batch ever started.** That is the signature.
Note that nothing here looks like an error, the rate is healthy, and the run went on to exit
successfully and write a profile — which is exactly why this needs an explicit check.

A **healthy** run instead:
* starts in `phase=floor`, and `sub=` climbs to `M/M`;
* prints `[keepgen]   continuous: floor complete, refs fixed -> refine` **after** that;
* only then shows `phase=refine`.

The **trigger** is visible near the top of the log:

```
[keepgen] RESUME(journal): reloaded 454420 cell-sides + fixed refs from ...raw.json.journal -> continuing
[keepgen]   continuous: refs restored from journal -> resuming refine (2s)
```

`+ fixed refs` → `resuming refine` means the run skipped the floor phase entirely. That is the only
path on which the bug fires. **A run that starts fresh cannot hit it.**

---

## 3. Why it happens (short version)

The sub-table batches were drained from exactly one place, and that place lived in the floor-phase
branch of the producer loop. A journal resume that restores *fixed refs* publishes `refs_ready`
**before** the loop starts, so the run begins in the refine phase, the floor branch never executes,
and the drain is never called. The exit condition checked only size-7, so the run finished normally
and wrote a profile with all sub-table work undone.

**How a journal with fixed refs gets there:** a `recommend`/scout run is an **R=1** floor-only probe,
but it writes to the *same* `.journal` path as a real generation. It samples every cell once, fixes
refs, journals them, writes its `.probe`, and exits — leaving exactly the poisoned journal. So the
sequence that breaks a deck is:

> **`recommend` (or any killed run that got past floor-complete) → then `complete` on the same deck.**

The damage: every bottoming sub-cell keeps **one rollout**. `DecideBottom` picks the kept sub-hand by
argmin over 6–26 such candidates (measured at mulligan 1/2/3), each carrying ~1.5 turns of noise — so
the bottoming policy selects on noise. It is worse than useless: a winner's curse picks the luckiest
estimate, not the best hand.

---

## 4. Fixed as of these commits — check your binary first

| commit | what |
|---|---|
| `8592cb18` | drain the sub-tables in the refine phase too; exit now requires `sub_remaining == 0`; raw records `meta.sub_target`; `mullgen.sh`'s artifact check fails on under-sampled sub-tables |
| `9f8d74fe` | a `recommend` scout no longer leaves a journal behind |

```bash
git log --oneline | grep -E "8592cb18|9f8d74fe"   # both present => your tree has the fix
```

**A binary built before those commits can still produce a starved table.** If the long-running job
was started from an older build, check it with §1 regardless of what your working tree says now.

---

## 5. If your run IS starved

**You do not have to start over.** The size-7 (keep) work is sound and the journal preserves it.

1. **Do not delete the journal.** It is the whole run.
2. Stop the run (this is the owner's call, not something to do unilaterally).
3. Build the fix: `./build.sh` — never bare `cmake`, which silently produces an unoptimised binary.
4. Resume exactly as before. The journal is re-accepted and the sub-tables are sampled this time,
   so you pay only for the sub-table deficit, not the size-7 work already done.

**Resuming after rebuilding is safe.** A journal resume validates `bucket_fp`, `deck_fp`, the rollout
config (depth / budget / max turns) and the **play digest** — *not* the commit string and not the
engine fingerprint. The fixes above are analyzer-only plus one flag-gated diagnostic, and play was
verified byte-identical, so the play digest is unchanged and the journal still matches.

Confirm before relying on it:

```bash
grep "play digest" <gen.log>        # must match the resuming run's digest
```

If instead you let a starved run finish, **do not rebaseline over it and do not ship it**. The
profile is presence-gated, so renaming it to `<stem>.keepmodel.exhaustive.profile.DISABLED.json` is
what deactivates it; the deck then falls back to static keep + lookahead bottoming, which is the
pre-existing validated behaviour.

---

## 6. Why the gate did not save the time

The confounded bottoming A/B **did** catch both bad tables and quarantined them, so nothing shipped —
and that gate is provably sound (measured: the confound costs the clairvoyant arm 0.4831 t and leaves
the blind arm at −0.0049 t, exactly as designed). But it only runs **after** generation, and each A/B
is another ~40 minutes. The artifact check added in `8592cb18` now catches the same defect from the
raw sidecar in seconds, before any games are played — and §1 catches it *during* the run, which for a
multi-day FiveColour-scale job is the only check that saves real time.

**Blast radius audit (2026-09-02), for reference:** only Dragons and Mirrorwing were affected. The
other 14 decks with tables all reached their intended target — Minotaur/Auras/Dragonstorm/
KittyEquipment/Anti-Lifegain/StompySurprise at 40, treasure_hunt 41, Knights/burn/slivers 60,
slivers_vial 100, Hinata2 19, Goblins/Creature Giving 2 (a legitimate adaptive floor, not starvation).

Related: `confounded-bottoming-gate-failures.md` (the full root-cause and the measurements).
