# The 2026-08-25 Mirrorwing overnight anomaly — UNEXPLAINED, cells void

**Status: OPEN.** One overnight run produced Mirrorwing numbers that no configuration on this
machine can reproduce. Four subsequent runs — including one with the *literal snapshotted binary
that produced it* — all reproduce committed ground truth exactly. The anomaly has not recurred.
Self-contained; everything needed to pick this up is here.

> **A SECOND, INDEPENDENT SIGHTING was recorded the same day** — see
> `regression-suite-determinism-anomaly.md`. Different tier (regression, seeds 2002/3003), different
> agent, found while adopting an unrelated flag; same deck, same all-improving direction, equally
> unreproducible (19 later full-scale executions). **It disagrees with this one on d0**: there d0 was
> byte-identical while only the searched cells moved, whereas here d0 diverged. That disagreement is
> the sharpest discriminator either record has — any proposed mechanism must explain both.

## What happened

`bash test/regression.sh --overnight` at 2026-08-25 01:29 (HEAD `11923ab9`, tree clean) returned
103 pass / 65 fail. Twelve of the failures were Mirrorwing — **all 12 cells, all in the improving
direction** — despite **zero engine changes since Mirrorwing's GT was accepted** (`994feba6`; for
this deck HEAD is byte-identical to that commit in both `src/` and `decks/Mirrorwing Dragon/`).

Per-game, on `mirrorwing_overnight_d0_s4004` (2000 games):

| | count |
|---|---|
| identical win turn AND play digest | 1733 |
| same win turn, digest differs (play changed) | 225 |
| win turn differs | 42 — **42 better, 0 worse** |

`d3_s5005`: 19 differ, 18 better / 1 worse. Distribution shifted toward faster wins (+10 turn-4 wins,
4 fewer unwon). Wins were almost all exactly **one turn earlier**.

## Why the obvious explanations are all dead

Every one of these was measured, not argued:

| Hypothesis | Refutation |
|---|---|
| Different binary | `test/logs/overnight/mtg.run` is md5-identical to the build; replaying the 12 cells with it gives **GT 12/12** (avg + digest) |
| Different deck list | 1733/2000 games byte-identical ⇒ same list, same shuffle |
| Different keep model | The bincache written *during that run* serializes a policy whose body is **byte-identical** to the known-good one |
| Older/newer keep model | R=30 → 5.9560, v1 → 6.2095, none → 6.2435, shedbase R=40 → 6.2095. Run was **5.9110**; correct is 5.9320. Nothing matches, and the run beat them all |
| `cards.json` (runtime-loaded!) | mtime 08-24 21:23, unmodified; nothing under `src/`or `decks/` changed during the run window |
| Bincache corruption | Valid / stale / absent / zero-tailed all give 5.9320. (Zero-tailed *was* a real bug — see below — but it scores 6.1630, i.e. **worse**, and the run was better) |
| Profile fallback | Default profile + real keep model → 5.9320 |
| Thread schedule | threads 1/4/12/24 all 5.9320; 16 identical concurrent jobs all 5.9320 |
| Batch composition | mirrorwing-only, all-14-deck d0, and full 168-job context all give 5.9320 |
| Result mis-assignment | **Zero** of the run's differing digests appear anywhere in the correct run's 2000 |
| Seed shift | Adjacent seeds span 5.9255–5.9320; 5.9110 is outside the range |
| Uninitialised memory | Valgrind: 0 errors over 60 d0 games |
| The three UAFs found (below) | Mirrorwing reaches **none** of them: no Goblin Lackey, zero look-effect cards, no Varchild's War-Riders |
| Play/draw assignment | `on_the_play = seed % 2` (`GoldFishRunner.cpp:774`), fixed per game |

`d0` diverged too, and d0 has no search and no budget — so it is not a timing or budget effect.
Compute was identical (925 ms vs 938 ms for the same 2000 games).

## The one contemporaneous oddity

`git reflog` shows commit `11923ab9` was made at **01:29:03** — another agent committing into this
same working tree at the second the batch started (subject: mulligan-profile generation, "probing
the digest"). So the tree was not quiescent. No deck artifact was modified, and the repo lives on
**9p (`v9fs`)**, where `rename()` is not atomic against an open fd. That is suggestive but
**not evidence** — no mechanism was demonstrated.

## What was found while hunting (real, fixed, but NOT the cause)

Commit `303c51be` — three heap-use-after-frees, all the same pattern (a reference into a
`std::vector` held across a `push_back` on that vector), found by ASAN over the full 168-job matrix:

1. `FireCombatDamageCheatIntoPlay` — Goblin Lackey → **Goblins**
2. `TopDispositionCandidates` — scry/reorder → **Hinata +**; the consequential one, it corrupts the
   *candidate set the search explores*
3. `PerformUpkeepCumulativeGifts` — Varchild's War-Riders → **Creature Giving**, in the rollout

Plus a bincache short-read guard in `MulliganProfileIO.h`. All byte-identical; ASAN re-sweep clean
(168/168, 0 errors). **None of these can be reached by Mirrorwing.**

## Standing instruction

**Do not `--accept` Mirrorwing overnight cells from that run.** Its GT is correct and reproducible;
re-run the cells rather than promoting the void data. If the anomaly ever recurs, capture
`test/logs/overnight/wins/` and the binary snapshot *before* anything else touches the tree — the
snapshot is what made the state question answerable at all.

## Next things worth trying

- ~~A harness guard: re-run a changed deck's cells once and fail loudly on a self-inconsistent
  result~~ — **RULED OUT 2026-08-26 (user): never run a tier twice; see
  viewer-feedback-2026-08-25.md's retraction.**
- Audit the remaining reference-across-mutation candidates (a crude static scan flagged 8 sites;
  most look like loop re-bindings, but ASAN only adjudicates paths that actually execute).
