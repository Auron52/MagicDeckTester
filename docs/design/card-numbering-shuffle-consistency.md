# Card numbering / CRN reshuffle consistency (batch == viewer == references == audit)

## Status (updated 2026-09-03): SHIPPED — committed WITH its GT rebaseline at b3f0bd55 (2026-07-20).
The "uncommitted / rebaseline pending" line below is the pre-commit record.

## Status: FIX IMPLEMENTED (uncommitted), verified on a 40-game Hinata d3 sample + 120-game Burn control.
Escape hatch `MTG_LEGACY_UNNUMBERED` restores the old behavior. GT rebaseline pending (see below).

## The bug

`Library::ShuffleByKey` — the mid-game **common-random-numbers (CRN) reshuffle** (fetchland/Ponder) —
keys each card on its stable per-copy id `c.m_number` ([src/core/Library.h](../../src/core/Library.h),
`ShuffleByKey`). Its whole purpose is to keep the realized future *consistent* across an A/B: each
card's rank depends only on its own `m_number`, so removing one card leaves every other card's relative
order unchanged.

But `m_number` **defaults to 0** ([src/core/Card.h](../../src/core/Card.h) `m_number = 0`) and the only
code that assigns it is `GoldFishRunner::AssignCardNumbers`, which was called **only when a log dir was
set or a forced-mulligan was active** ([src/runner/GoldFishRunner.cpp](../../src/runner/GoldFishRunner.cpp)).
The **batch never numbered** ([src/runner/BatchRunner.cpp](../../src/runner/BatchRunner.cpp) passes `{}`
to the logger and never touches `state` numbering).

Consequence: in the **batch** (= ground-truth generation) every card had `m_number == 0`, so every
`ShuffleByKey` key was equal and `std::stable_sort` was a **no-op** — the "reshuffle" left the library
in place. In the **viewer / references / audit** (`--log-dir` / `--claude-play`) cards were numbered
1–60, so the same reshuffle was **real**. The two disagreed for any deck that reshuffles mid-game
(Hinata via Ponder, Anti-Lifegain via fetch), which is why the per-game audit (`test/explain_game.py`)
could not reproduce a batch game for those decks — the summary had mis-attributed this to a "value-leaf
gap," but `--ignore-play-profile` never dropped the value model; the real perturbation was card
numbering feeding `ShuffleByKey`.

## The fix

Number cards **always**, at the single setup choke point `GoldFishRunner::SetupGame` (right after the
opening Fisher-Yates shuffle), gated by an escape hatch:

```cpp
static const bool s_legacy_unnumbered = std::getenv("MTG_LEGACY_UNNUMBERED") != nullptr;
if (!s_legacy_unnumbered) { AssignCardNumbers(state, BuildCardNumbering(deck)); }
```

Because every runner (batch / goldfish CLI / analyzer / `RunClaudePlay`) funnels through `SetupGame`,
they now all number identically → `ShuffleByKey` is real and consistent everywhere. `RunClaudePlay`
([src/main.cpp](../../src/main.cpp)) also numbers explicitly right after `SetupGame`, so the
viewer/references stay numbered even under the hatch (the hatch only reverts batch/goldfish for A/B).

Numbering is post-shuffle-order based (identical to the old logging path) and independent of the
opening Fisher-Yates shuffle, so **opening hands are unchanged**; only the mid-game reshuffle moves.

## Verified impact (build/Release/mtg, this fix)

| check | result |
|-------|--------|
| batch == goldfish (no `--log-dir`), Hinata d3 s3003, 40g | **0/40 differ** |
| goldfish no-logdir == goldfish `--log-dir` | **0/40 differ** |
| single-game `--game-index gi` == batch (gi 0,5,12,27,33) | **5/5 match** |
| Burn (non-reshuffle) numbered vs `MTG_LEGACY_UNNUMBERED` | **0/120 differ**, avg 4.4250 both |
| Hinata numbered vs legacy | 8/40 differ; **7 of 8 favor numbering** (real Ponder reshuffle finds combo pieces); avg 5.85 vs 6.30 (−0.45 on the sample) |

So the audit now faithfully reproduces batch games; on the metric only **reshuffle decks (Hinata,
antilife) move**, and for Hinata the correct (real) reshuffle also plays better. The digest folds
`card_num`, so **every** deck's digest refreshes (previously all-zero → now real per-copy ids); this is
a cosmetic fingerprint change for non-reshuffle decks (win-turns identical).

## Smoke rebaseline (numbering-only, isolated; rank fix stashed) — ACCEPTED

Ran `regression.sh --smoke` with the numbering-only binary vs HEAD (unnumbered) committed GT:

- **Non-reshuffle decks (slivers, burn, th, knights): 0 per-game win-turn diffs** across all 12 cases
  (d0/d3/d5). Only the digest refreshed. Confirms zero gameplay effect where nothing reshuffles.
- **Reshuffle decks improved substantially** (real fetch/Ponder reshuffle):
  | case | old avg | new avg | Δ | loss→win / win→loss |
  |------|---------|---------|-----|-----|
  | antilife d3 | 4.648 | 4.220 | −0.43 | 11 / 1 |
  | antilife d5 | 4.633 | 4.180 | −0.45 | 6 / 0 |
  | hinata d3 | 6.013 | 5.753 | −0.26 | 7 / 1 |
  | hinata d5 | 6.040 | 5.733 | −0.31 | 5 / 0 |
  (d0 for both moved many games as reshuffle-variance but ~0 net avg; d0 is greedy/non-gating.)
- **2 searched win→loss** — both root-caused as reshuffle-variance and reproduced *purely from
  numbering* via the `MTG_LEGACY_UNNUMBERED` hatch (no `--log-dir`, which would force numbering):
  - hinata_smoke_d3 gi89: unnumbered=**7**, numbered=**loss**
  - antilife_smoke_d3 gi227: unnumbered=**5**, numbered=**loss**
  Each is a different physical game (draws diverge post-reshuffle), the minority tail of a strongly
  net-positive change. Accepted with `--accept-with-regressions` (note in GT provenance header).

**Hatch gotcha noted:** `--log-dir`/forced-mull still call `AssignCardNumbers` in the GoldFishRunner
logging path, which *overrides* `MTG_LEGACY_UNNUMBERED`. A true unnumbered run must omit `--log-dir`.
(Left as-is: the viewer/references MUST stay numbered even under the hatch — this is the mechanism.)

## Regression rebaseline (numbering-only, isolated) — ACCEPTED

`regression.sh` (seeds 2002/3003) vs HEAD unnumbered GT. Same pattern as smoke:
- **Non-reshuffle decks (burn/slivers/th/knights): 0 win-turn diffs** across ALL searched cases (digest-only).
- **Reshuffle decks strongly net-positive** (searched): hinata d3 +13/−1 (s2002) & +14/0 (s3003);
  hinata d5 +6/−1 & +7/0; antilife d3 +7/−1 & +9/−3; antilife d5 +5/0 & +8/0.
- **6 searched win→loss**, each confirmed to flip PURELY from numbering via the hatch A/B
  (unnumbered = committed GT win, numbered = loss): hinata_d3/d5_s2002 gi39 (8→loss),
  antilife_d3_s2002 gi46 (6), antilife_d3_s3003 gi107 (6)/gi130 (5)/gi245 (5). Accepted with note.

## Overnight rebaseline (numbering-only, isolated) — ACCEPTED

`regression.sh --overnight` (seeds 4004–7007; 72 jobs, 90.8k games, 98 min). Same pattern:
- **Non-reshuffle decks: 0 win-turn diffs** across all overnight cases EXCEPT one — slivers_overnight_d0
  gi1064 (5→4). Hatch A/B shows unnumbered==numbered==4 → **numbering-INDEPENDENT pre-existing GT
  staleness** (current HEAD code gives 4 regardless), a benign d0 improvement the rebaseline corrects.
  (Note: `ShuffleAfterSearch` fires after ANY library search, so any fetch/tutor deck is affected —
  the "only antilife+hinata" comment in SpellEffects.h is imprecise; slivers has a rare search.)
- **Reshuffle decks MASSIVELY net-positive** (searched): antilife d3/d5 loss→win 22–43/case vs 1–3
  win→loss, faster 273–321 vs slower 104–123; hinata loss→win 15–31 vs 1–4 win→loss.
- **35 searched win→loss**, all reshuffle decks; 6 spot-checked via hatch A/B all flip purely from
  numbering (unnum=GT win, num=loss). Accepted with note. **14 total blockers hatch-confirmed across
  all three modes** (2 smoke + 6 regression + 6 overnight) — every one a pure-numbering flip.

## Commit plan

- **Commit A (numbering fix):** `src/runner/GoldFishRunner.cpp` + this doc + all rebaselined GT
  (`test/regression_gt.txt` + `test/gt_logs/*.wins`, all three modes). NO push.
- **Commit B (rank fix):** restore rank-fix+gate from `stash@{0}` (code only), rebuild, rebaseline
  hinata all modes vs the numbered baseline (clean), accept, commit. Then gate root-cause.

## GT rebaseline plan (historical)

- All GT digests change (numbering folded in); reshuffle-deck avgs/win-turns move.
- The audit gate will flag reshuffle-deck win→loss games, but they are **different physical games**
  (draws diverge at the reshuffle point) — benign variance, the classifier already distinguishes them.
- Sequence per `.claude/skills/regression-testing.md`: run the suite once → inspect per-case diffs →
  `regression.sh <mode> --accept`. This also saves a **numbered** baseline snapshot so future audits
  diff numbered-vs-numbered (clean like-for-like).
- Interacts with the uncommitted **rank fix** (also changes Hinata) — rebaseline them together.
- **Mulligan sidecars:** opening hands are unchanged, but exhaustive keep/bottom *rollouts* that reach
  a mid-game reshuffle now reshuffle for real. Whether this shifts any bottoming decision for
  reshuffle decks (Hinata/antilife) needs a check before their sidecars are trusted cross-commit.
- The prior "4 regression win→loss" and the "gate −0.052" were measured under the **unnumbered**
  binary; both must be re-derived under the numbered binary.
