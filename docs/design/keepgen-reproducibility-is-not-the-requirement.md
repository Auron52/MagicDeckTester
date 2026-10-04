# Reproducibility is not the requirement — engine identity and a pinned threshold are

**Status: PROPOSED (user-directed 2026-10-03). Not implemented.**

> *"I'm starting to think I don't care that much about reproducibility for normal mulligan profile
> generation. Maybe we could make this a setting or something, but if it is just to have a profile for
> a new deck rather than to compare profiles having more games played is unlikely to be worse. Am I
> wrong on that?"* — USER

**Largely right.** Byte-reproducibility of the raw is enforced far more widely than anything depends
on, and the enforcement has already destroyed days of compute. But the premise "more games is unlikely
to be worse" has **one measured counterexample**, and it determines what must be kept.

## 1. What actually depends on determinism — audited field by field

The resume gate is a short-circuit chain: `bucket_fp && deck_fp && seed_base && K && max_mull &&
equiv_seed && R && PlayIdentityAllows(...)`.

| field | what it protects | verdict |
|---|---|---|
| `play_digest` | the rollouts were produced by THIS engine's play | **KEEP GATED.** Not a sampling question (below) |
| `bucket_fp`, `deck_fp`, `K` | cell indices mean the same hands | **KEEP.** A mismatch mis-addresses cells; cheap to check |
| `equiv_seed`, `max_mull` | the table's shape | **KEEP.** Same reason |
| `seed_base` | matching = resume, differing = poolable disjoint samples | **KEEP.** Already does the right thing |
| **`R` (the cap)** | nothing about validity — it is a PRECISION ceiling | **RELAX.** See §2 |

**Nothing in the chain enforces equal SAMPLE COUNTS, and nothing needs to.** A cell's value is the
mean of rollouts `r = 0..n-1`, and a rollout is a pure function of `(seed_base, r, w, pd)` — so two
runs with different `n` hold **prefixes of the same sequence**. Resume already accepts arbitrary `n`
per cell. Cross-machine pooling already sums unequal counts, because the handoff allocates **disjoint**
seed bases. So sample-count determinism was never a requirement; it was an accident of the gate also
checking `R`.

## 2. The highest-value relaxation is `R`, and it is the one that destroyed a journal

On 2026-10-02 a 350 MB / 5.58 M-record journal was lost because it was rolled at `fast` (R=30) and the
run was launched as `complete` (R=40). `keepgen-journal-recipe-must-match-R` records the incident.

**That refusal protects nothing.** Raising the cap mid-stream is *strictly more samples*: the floor
`r0` is unchanged, every banked value is still the same prefix, and the extra `r` values are simply
admissible where they were not before. Resuming an R=30 journal at R=40 is exactly the "more games
played" the user describes — and the gate refuses it, discarding everything.

**Proposed: `R` becomes a cap the resume RAISES to, never a field it matches on.** A journal at R=30
resumed at R=40 continues to 40. A journal at R=40 resumed at R=30 keeps its deeper cells (they are
already converged past the new cap) and simply stops refining — a cap is a ceiling, not a target.
This needs no flag: it is a strictly-more-work relaxation of a check, and it removes the sharpest
foot-gun in the system.

## 3. The counterexample — where "more games" CAN be worse

The freeze rule is **adaptive against a global threshold**, and that threshold is re-derived as
sampling advances. `ExhaustiveKeep.cpp`:

> *"ROLLING vg … re-derived from every cell's accumulators as the completed frontier advances, so
> re-deriving it after a resume follows a different trajectory than the uninterrupted run and **moves
> freeze verdicts**. Measured on burn: pinning it (`MTG_KEEP_REFS_OFFSET=0`) removed 98% of the cells
> that resumed **UNDER-sampled (539 → 9)**."*

A different sampling trajectory made **539 cell-sides freeze EARLY, on noisier estimates.** So
nondeterminism does not only add samples — through the stopping rule it can *remove* them.

**This does not rescue byte-identity. It localises what must be kept:** the THRESHOLD, not the raw.
`refs`/`vg_roll` are already journaled and restored precisely for this. **Keep that pinning
unconditional** and the user's relaxation is safe; drop it and "more games" becomes a coin flip per
cell. Quality protection and reproducibility are separable, and only the first is load-bearing.

## 4. The one thing that is genuinely worse: mixing ENGINE versions

Cells rolled by a different engine are not "more samples" — they are samples **of a different game**.
Stale, not noisy; averaging them in moves the table toward play the engine no longer performs, and no
amount of extra sampling corrects it. Measured the same day: origin's engine gave play digest
`86004bde6b4b45ae` against this branch's `5672dd070faaa160` on the same deck.

So `play_digest` stays gated, `MTG_KEEP_RETAIN_FOREIGN` stays user-only and per-digest, and the
mixed-provenance machinery (per-record `prov` stamps, the artifact self-declaring mixed) stays as the
deliberate escape hatch it already is. **The distinction to hold onto: relax SAMPLING identity, keep
ENGINE identity.**

## 5. Squaring this with `keepgen-no-off-switches.md`

That doc is a user-directed ruling that generation must be continuous, always-reporting and
always-incremental, *"and there must be no option that turns any of those three off."* A new
reproducibility setting has to be squared with it. It is:

* **It is not an execution-path switch.** The ruling's own test is the one it applied to two flags:
  *"`MTG_KEEP_R_FLOOR` is deleted (it exists only to defeat the pool). `MTG_KEEP_ROLLOUTS` stays: it is
  the cap-R / precision lever, not an execution-path lever."* Admission policy changes nothing about
  the pipeline — same pool, same journal, same reporting.
* **It SERVES the third protected property rather than defeating it.** "Always incremental" is stated
  as *"any interruption resumes, nothing is repaid."* The gate as written repays **everything** on an
  `R` mismatch. Relaxing it is the ruling's goal, not an exception to it.
* **It follows an existing precedent in the same document.** §5 made probe carry always-on because it
  is *"already fingerprint + play_digest gated, so a mismatch is ignored rather than misapplied."*
  **Ignore-on-mismatch is the established pattern; the journal gate refuses instead.** That
  inconsistency is the defect.

## 6. So: is a setting needed? Mostly no — prefer changing defaults

The user offered *"maybe we could make this a setting or something."* Minimise it:

* **`R` relaxation (§2): no flag.** Strictly-more-work; make it the behaviour.
* **`refs`/`vg` pinning (§3): no flag, unconditional.** It is a quality guarantee.
* **Cross-engine mixing (§4): the flag ALREADY EXISTS** and is correctly user-only and per-digest.
* **A "this artifact is for pooling/comparison, be strict" mode: NOT NEEDED.** Pooling works on
  unequal counts via disjoint seeds, and profile-vs-profile comparison is measured by PLAYING GAMES
  (`test/keepmodel_exhaustive_ab.sh`), which never reads a raw. No consumer needs sample-count
  identity.

Net: **one behaviour change, no new flags.** That is the shape the no-off-switches ruling wants.

## 7. The "strip extra games from each side" idea — sound, but solving a problem we do not have

> *"Or we could do a comparison that strips off extra games from each side?"* — USER

**The reasoning is correct.** Because a rollout is pure in `(seed_base, r, w, pd)`, two runs' cells are
prefixes of one sequence, so truncating both to `min(n_A, n_B)` yields **bit-identical** prefixes.
Comparability is recoverable after the fact, without constraining generation. That is a genuinely good
observation and it is the reason §1's relaxation is safe rather than merely convenient.

Two practical notes:

1. **It needs per-rollout values, which the raw does not store.** The raw holds `sum`/`sumsq`/`cnt`;
   rollouts cannot be subtracted back out of a sum. It would need the individual values — which is
   exactly what `keepgen-durable-precompute.md` proposes to start persisting, so the two compose.
2. **No current consumer needs it** (see §6). **Do not build it speculatively.** Record it here as the
   known-good technique for the day a comparison genuinely requires equal counts — at which point it is
   a post-processing pass over per-rollout values, not a constraint on the generator.

## 8. Verification if this is implemented

* The existing control-vs-SIGKILLed-twin harness (`keepgen-resume-exactness.md`, ~5 min) asserts `cmp`
  on the final raw. **That assertion must change, not be deleted:** under §2 a resumed raw is no longer
  byte-identical to the control by design. Replace it with (a) every cell's value equals the mean of
  the prefix its `n` claims, and (b) **no cell is under-sampled relative to the control** — which is
  the §3 regression, stated as a test.
* Add the R-crossing case directly: roll a journal at R=30, resume at R=40, assert cells advance past
  30 and that no banked cell was discarded.
* `scripts/mullgen.sh`'s preflight (which currently REFUSES an `R` mismatch and names the recipe that
  would resume it) becomes a note rather than a refusal.

Related: `keepgen-no-off-switches.md` (the ruling this is squared against),
`keepgen-durable-precompute.md` (the companion change; §7 depends on it),
`keepgen-producer-barrier-and-durability.md`, `keepgen-resume-exactness.md` (the harness),
`.claude/skills/mulligan-profile.md` (the handoff/pooling protocol and its disjoint seed allocation).
