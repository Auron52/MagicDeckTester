# Deferred: exhaustive-profile workflow improvements (post-Hinata)

Two items queued 2026-07-07 to tackle **after** the Hinata generation pipeline is ready/running. Neither
blocks the Hinata freeze. Recorded here (not private notes) per the repo's deferred-work convention.

---

## 1. Decouple "profile under test" from "profile that drives GT / rollouts" (auto-attach churn)  — DONE 2026-07-07

**Resolution.** Added a per-context override `MTG_EXHAUSTIVE_PROFILE` inside `AttachExhaustiveSidecar`
(`src/ai/MulliganProfileIO.h`) — one place, so all three call sites (both play paths + BatchRunner)
inherit it:
- `MTG_EXHAUSTIVE_PROFILE=none|off|0|""` → attach NOTHING (a genuine static baseline arm).
- `MTG_EXHAUSTIVE_PROFILE=<path>` → attach THAT profile's exhaustive block (candidate under test),
  without placing a `.gz` next to the deck.
- unset → the presence-gated auto-attach below (the committed/adopted sidecar; runtime + suite GT).

The env is only consulted when the loaded profile has no exhaustive block yet — a profile loaded via
`--profile <exhaustive>` keeps its own block (early return before the env check), so `none` is a safe
no-op there. Both A/B harnesses now set `MTG_EXHAUSTIVE_PROFILE=none` on the run (`ab()` in
`keepmodel_exhaustive_ab.sh`; the static-baseline arm in `keepmodel_r_sweep.sh`), retiring the old
mv/scratch-path dance. Verified on burn: unset==explicit-path (attach reproduced), none==empty (static),
unset≠none (attach matters), and none is a no-op when the profile carries the block.

**Open question — RESOLVED (generation is already invariant to adoption).** Generation loads
`<deck>.profile.json` (the *static* profile) at `src/analyzer/main.cpp:271-274` and never calls
`AttachExhaustiveSidecar` (it's play-only, by design — comment at `MulliganProfileIO.h:575`). So an
adopted `.gz` at `decks/` does NOT perturb generation rollouts / `play_digest`, and **chunk pooling
survives adoption**. The "invalidates chunking" concern was purely the play-side GT churn + A/B
contamination, both of which the override addresses. No change to generation was needed.

**Workflow going forward.** Test candidate profiles via `MTG_EXHAUSTIVE_PROFILE=<candidate-path>` (never
place a candidate `.gz` at `decks/`); the committed profile and its GT only move when you deliberately
adopt (install the `.gz` at `decks/` + re-baseline once).

**Problem (original).** The exhaustive profile is **auto-attached by presence**: `AttachExhaustiveSidecar` layers
`decks/<deck>.keepmodel.exhaustive.profile.json[.gz]` onto *any* run of that deck. That one mechanism
couples things that should be independent:
- **A/B baseline contamination** — a static-vs-exhaustive A/B run from `decks/` attaches the exhaustive
  profile to the *static* arm too → both arms identical → false 0.0 delta. (Hit + worked around 2026-07-06
  by pointing `KM_EXH_PROFILE` at a scratch path and keeping the `.gz` out of `decks/` during the A/B.)
- **Regression GT coupling** — the suite attaches whatever `.gz` sits at `decks/`, so the deck's GT is
  tied to the current profile. Improving the profile (pool more chunks → higher R → new `.gz`) changes
  the numbers → GT must be re-baselined *every* time → churn.
- **"Invalidates chunking"** (user's phrasing) — adopting/improving churns GT and makes it awkward to keep
  chunking + testing intermediate profiles cleanly. (Confirm the exact failure when tackling: e.g. does
  an adopted `.gz` at `decks/` perturb *generation* rollouts, changing `play_digest` so new chunks won't
  pool with old ones? If so that is the sharp edge — generation must ignore the adopted exhaustive block.)

**Fix direction (user: "ensuring the profiles are the same when testing").** Make the active profile
**explicit per context** instead of presence-gated:
- A test-time override, e.g. `MTG_EXHAUSTIVE_PROFILE=<path|none>`, that pins (or disables) the exhaustive
  profile the suite / A-B uses, so the baseline is controlled and profile improvements don't churn GT
  until *deliberately* adopted. The A/B harness's baseline arm sets `none` (or hides the sidecar) so it is
  genuinely static; the candidate arm points at the path under test.
- Keep only the **committed/adopted** profile auto-attaching (runtime); candidates always go through an
  explicit path. Consider a `keepmodel.exhaustive.candidate.*` naming that is NOT auto-attached.
- Verify generation rollouts do NOT consume the adopted exhaustive block (so `play_digest` — hence
  chunk poolability — is invariant to whether the deck has been adopted). If they do, clear it in the
  rollout profile the way `keep_model` is already cleared.

**Value:** clean A/Bs without the scratch-path dance; regression GT that only moves when you choose to
adopt; and chunk pooling that survives adoption.

---

## 2. Cross-run table pruning: freeze decisively-settled cells, spend rollouts on the ambiguous ones

**Idea.** Today adaptive sampling (`MTG_KEEP_R_FLOOR` + refine waves) skips refining confident cells
*within a run*. Extend that **across runs/chunks**: use a prior run's measured confidence to **freeze**
cells whose keep/mull (and bottoming) decision is decisively settled, so future chunks (regeneration or
higher-R pooling) don't re-spend rollouts re-establishing them — they concentrate on the still-ambiguous
cells. The user framed this as "adopting a cut-off of areas of the table when we regenerate," and
crucially as a **less dangerous version of hand-authored prune rules**: the cut is **data-driven**
(only cells the measured data says are confidently decided), so it cannot prune a cell that is actually
live — unlike a human rule ("always mulligan 0-landers") that can be wrong on edge cases.

**Sketch.**
- After a run/pool, classify each cell: decision margin = |keep_val − M[m+1]| in units of the cell's SE
  (`sumsq`-derived, now that sumsq survives the merge). A cell is **frozen** if the margin exceeds a
  conservative multiple of its SE **and** accounts for the optimizer's/winner's curse (min/argmin bias)
  — i.e. it cannot flip even under the worst-case bias at the current R.
- Emit a **prune set** artifact (list of frozen comps + their pooled values). Generation reads it
  (`MTG_KEEP_PRUNE_SET=<file>`): frozen cells are sampled at floor (or skipped) and their carried values
  reused; only unfrozen cells get the full rollout budget.
- Pooling stays valid: frozen cells simply accrue no new counts in later chunks; the merge already sums
  per-cell over whatever files contain them.

**Open questions (resolve before building).**
- Freeze threshold vs the curse: how many SE of margin is safe? Bottoming sub-cells (argmin over subhands)
  are more R-hungry than the keep bit — likely freeze keep cells earlier than bottoming cells, or don't
  freeze bottoming at all until high R.
- Does freezing bias the pooled `D_opt` reporting? (Frozen cells contribute a lower-R estimate; fine for
  the decision, but note it in the projected-regret readout.)
- Representation: prune set keyed by (size, comp); must share the deck/bucket fingerprints so it can't be
  applied to a mismatched run.
- Interaction with cross-machine pooling: a prune set derived on machine A should be shareable to B (same
  fingerprints), or each machine derives its own from the shared pool.

**Value:** for an expensive deck like Hinata (~600–750k cells, ~40 rollouts/s), most 7-card hands are
junk (confident mulligans) — freezing them after the first pass could cut a large fraction of later-chunk
rollouts, turning a multi-day run materially shorter without the fidelity risk of hand-authored rules.

**Related:** supersedes the "user-defined prune rules" idea (memory: keepmodel scale-runs / user-rules
tiers) — same goal (skip hopeless hands) but grounded in measured confidence rather than authored
heuristics.
