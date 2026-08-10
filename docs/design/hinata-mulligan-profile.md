# Hinata exhaustive mulligan profile (R=22) — GENERATED + VALIDATED

2026-07-25. Exhaustive bucketed keep+bottom mulligan profile for the Hinata deck, generated on the
frozen commit `7f3aaa8` and validated by the in-game A/B before adoption. Follows
`.claude/skills/mulligan-profile.md`.

## Generation

- **Feasibility:** K=**20** buckets (60-card deck → 20 equivalence classes; the 7 lands stay distinct
  by role, cantrips/dig stay distinct). **604,702 distinct hands** (size7 = 431,144). At d3/b10 with
  the value leaf attached, one R=1 mm6 full pass ≈ 21 h — the reason this was a multi-week grind.
- **Build driver (historical):** the `test/exhaustive_chunked_gen.sh` round driver cited below was **deleted 2026-08-10** (`docs/design/keepgen-no-off-switches.md`) -- its rounds ran the uniform, barriered, non-journalled path. The record is kept as-is; a rebuild today is `--gen-mulligan complete`, re-run to resume.
- **Build:** `test/exhaustive_chunked_gen.sh` (`KM_TARGET_R`, `KM_ROUND_R=1`, mm6), frozen `7f3aaa8`,
  d3/b10, `MTG_EQUIV_SEED=20260701`. Pooled **R=22** on the live frontier from **22 disjoint sidecars**:
  12 adaptive rounds (this box, seeds 30001000–30001011) + 9 budget-10 chunks (second box, seeds
  40001002–40001010) + 1 budget-20 chunk (second box, seed 40001001, force-pooled at adoption only).
  D_opt(draw) = **5.9135**, D_opt(play) = 6.3136.
- **Gen-only prune cutoffs (Phase 1+2), on branch `origin/hinata-gen-7f3aaa8`** (`efe67e3` low-P size-7
  cutoff, `766006a` sub-table cutoff): play-neutral (play_digest `9077572f` preserved → chunks pool),
  decision-preserving. Cut ~**42 %** off each round's wall clock (round 8 72,856 s → round 10 42,556 s
  with both active) without restarting the pool. Left on the dedicated gen branch (not folded into the
  main line); grab with `git cherry-pick efe67e3 766006a` for other mulligan gens.
- Artifacts staged in `logs/Hinata2_gen/`: pooled profile 298 MB → committed gzipped **11 MB**; raw
  sidecar committed gzipped **7.3 MB** for future pooling.

## Validation A/B (16 seeds, 1000 games, budget-20, avg9 metric)

`avg9` (non-wins counted as turn 9) is the objective the profile minimizes; win/loss rate alone is not
the metric. Negative = exhaustive wins earlier.

| gate | d1 | d2 | d3 | note |
|---|---|---|---|---|
| **keep** (exhaustive vs baseline) | **−0.106** (16/16) | **−0.097** (16/16) | **−0.094** (12/12) | clairvoyance-free (lookahead bottoming both sides) |
| **bottom** (blind vs lookahead, `MTG_CONFOUND_BOTTOM`) | **−0.069** (16/16) | — | — | blind ≥ lookahead confirmed once peek removed |

**Baseline caveat (important):** unlike Slivers/Knights (which A/B'd against a *trained static*
profile), Hinata skipped the static grid — its baseline `Hinata2.profile.json` is essentially the
**default/heuristic** keep. So the −0.10t keep win is exhaustive-vs-**default**, which is why it is
larger than the ~0.03t exhaustive-vs-tuned-static gains on other decks. It is the correct number for
*this* adoption (the default is what Hinata would otherwise ship), but not directly comparable to other
decks' margins.

d3 was the gen-native depth but ~20 min/cell on this box (heavy escalation code) — d1/d2 ran full
16/16; d3 was stopped at 12/16 complete seeds (all consistent, −0.094t) once the win was clear.

Harness note: `test/keepmodel_exhaustive_ab.sh` needs `--ignore-play-profile` to tolerate the enabled
`value_play` block (its explicit `--depth` sweep is otherwise rejected) — fix committed upstream as
`e74536a`. The ~6 GB profile-load footprint that OOM'd this box was fixed by `82859f7` (share +
stream-parse); the runtime transparently decompresses the committed `.gz`.

## Adoption

Committed gzipped sidecars next to the deck
(`decks/Hinata2/Hinata2.keepmodel.exhaustive.{profile,raw}.json.gz`). Keep+bottom are presence-gated →
active immediately for Hinata; byte-identical for all other decks. The R=22 profile was accepted on the
current play logic (`64689ee`) rather than regenerated for it — deferred as an acceptable staleness.

**GT rebaseline:** unlike Auras, **Hinata IS in the regression suite**, so adopting shifts its GT. The
shift is the honest blind-bottoming tradeoff (the unconfounded goldfish metric rewards the lookahead
peek, so enabling blind bottoming nudges win-turn up on mulligan games) plus the keep improvement —
accepted via per-game audit, not a regression.
