# Goblins value model: isolating its effect from the GT drift

**Status 2026-08-01: DONE.** All three GT tiers are current — smoke + regression at `e36f3bb`,
overnight accepted after every changed case was attributed (tier net **−0.03144** over 16,000 games).
The value model is **measured and settled**: a real improvement in its shipped configuration, a real
(smaller) regression at the d3 test point.

**One thing is deliberately left open and is being handled elsewhere:** Goblins plays at a depth its
value-leaf table was never measured at, so the cell deciding its modal search outcome is padding, not
evidence. See *The unmeasured cell* below.

Context: `8a3bbb1` added `decks/Goblins/Goblins.value.json` (a learned value model plus the adopted
`value_play` config). Four goblins cases in smoke/regression and twelve in overnight then failed, and
the question was whether the model is a quality regression at searched depths.

## The confound: a GT diff is not an A/B

The overnight failures looked like a searched-depth regression (+0.0030 at d3 and d5). They are not
attributable to the value model, because the goblins **overnight GT was last accepted at `9462479`**
("re-accept overnight GT after land-tiebreak rebase") — which predates *four* commits, among them
`ec70359`'s search-perf heuristics. Comparing today's binary against that GT measures all of them at
once.

**Check GT provenance before attributing a diff:**

```bash
git log --oneline -3 -S "<deck>_<mode>_d<depth>_s<seed>=<the old value>" -- test/regression_gt.txt
git log --oneline <that sha>..HEAD -- src/ai src/core src/cards     # what else moved since
```

Note `-S` finds when a *string* was added, so a case whose value did not change at the last accept
traces back further than the accept itself. Goblins' d0 values trace to `ee93f54` for that reason;
the actual last overnight accept is `9462479`.

The clean measurement holds the binary fixed and toggles only the thing under test:

```bash
cp -r decks/Goblins /tmp/gob_nv/ && rm -f /tmp/gob_nv/Goblins/Goblins.value.json
# both arms, ONE pooled batch, job shape mirroring test/regression.sh's manifest emitter
```

## THE TRAP: overlapping base seeds silently collapse the sample

**A game's identity is `base_seed + game_index`.** A job with base seed `B` and `N` games therefore
covers effective seeds `[B, B+N-1]`. If consecutive jobs' base seeds are spaced by less than `N`,
**the jobs replay the same games**, and every "independent seed" is the same handful of games counted
over and over.

The first large run here spaced base seeds by **1** with **1000 games per job**:

```
d5: bases 100001..100100 x 1000 games  ->  claimed 100,000 games
                                           ACTUAL 1,099 distinct (each seen up to 100x)
d3: bases 200001..200030 x 1000 games  ->  claimed  30,000 games
                                           ACTUAL 1,029 distinct
```

It reported **−0.00130 at −14.4σ**. On distinct games the same data gives **−0.00273 ± 0.00203
(1.3σ)** off *five* changed games. The σ was pure replication.

**The tell** was d3: all 30 seeds worse by *exactly* +0.001, zero variance. An average over exactly
1000 games is an integer turn-sum / 1000, so +0.001 is exactly one turn — and getting exactly one in
30 independent seeds is ~1e-13. It was one game: `200001+587 = 200002+586 = … = 200588`, identical
digest in all 30 files.

**Rules:**
- Space base seeds by **at least the games-per-job** (`base = S0 + i*N`) so jobs tile the seed space
  exactly once. Assert it: `distinct(base+gi) == sum(games)`.
- Analyse by **effective seed**, not by job. Keying on `base+gi` makes overlap impossible to miss and
  costs nothing when there is none.
- Treat a **zero-variance** paired result as a bug until proven otherwise.

The earlier 12-seed sweep (`11011, 12012, … 23023`) was spaced **1001** against 1000 games/job, so it
tiled cleanly by luck — 12,000 distinct games per arm, and its −0.0018 (p=0.033) stands. It was
*ten times larger* than the "260,000-game" run that superseded it.

## The measurement

510 jobs / 510,000 games, base seeds spaced 1000, at `bbed1e5`. Deck copy ± `Goblins.value.json`,
same binary. Arms (verify with the `[play]` lines in `batch.err`):

```
old_d5     depth=5  source=default(depth)+cli(budget)      the GT baseline
old_d6     depth=6  source=cli                             depth bump alone
new_d6     depth=6  source=value_play(depth)+cli(budget)   what actually ships
old_d5pin  depth=5  source=cli          10 jobs, sanity: must equal old_d5 exactly
d3 old/new depth=3  source=cli(--ignore-play-profile)
```

Sanity arm: **0 of 10,000 games differ** from `old_d5`. No seed overlap on any arm
(distinct == job-games == 100,000).

| comparison | play lines differ | turns differ | better/worse | mean delta | |
|---|---|---|---|---|---|
| depth 5→6, no model | **2** (0.00%) | **0** | 0 / 0 | 0.00000 | — |
| **model at matched d6** | 3,063 (3.06%) | 904 (0.90%) | 538 / 366 | **−0.00136** ± 0.00032 | **−4.2σ** |
| package (= the suite's d5 case) | 3,063 | 904 | 538 / 366 | −0.00136 ± 0.00032 | −4.2σ |
| **d3** (`ignore_play_profile`) | 576 (0.58%) | 101 (0.10%) | 30 / 71 | **+0.00041** ± 0.00010 | **+4.0σ** |

**The d5→d6 bump is a no-op.** At a 40 ms budget the search is budget-limited, not depth-limited, so
the extra ply changes 2 play lines in 100,000 games and no outcomes at all. The package effect is
therefore entirely the model, and the "arms run at different depths" confound — real, and worth
checking — turns out to cost nothing here.

**The model helps where it ships and hurts at d3.** d3 pins `ignore_play_profile`, which bypasses the
play *config* (depth/budget) but **does not stop the value leaf from firing** — 0.58% of d3 games take
a different line. The model was validated at d6; at d3 it is being consulted at a depth it was never
tuned for, and it costs +0.00041. That is ~1/3 the size of the d5 gain, and d3 is a suite test point,
not a shipped configuration.

Unwon games (`turn = -1` in a `.wins` file) went 61 → 105 in the new arm. All 46 won/unwon flips are
**turn-7 or turn-8** games, so each costs only +1 or +2 against the `max_turns+1 = 9` score — they are
the +1/+2 bars of the histogram, not a hidden tail.

## Other ways to measure the wrong thing here (all hit during this investigation)

1. **`classify_turn_later.sh` only re-runs the NEW arm.** It reports `churn` vs `PERSISTS` for a
   searched slowdown, but never asks whether the OLD side's faster win survives more search. When the
   old side is itself budget luck, `PERSISTS` reads as "real regression" when the truth is the
   opposite. `goblins_smoke_d5_s1001` gi43 was exactly this: T5 → T6 at the 20 ms case budget,
   `PERSISTS` at 4× and 16× — but the *old* arm also becomes T6 at 80 ms and stays T6 at unlimited.
   **Run BOTH arms at `--budget-ms 0` before calling a searched slowdown real.**
2. **`--budget-ms 0` means unlimited; a big number does not.** The budget is a deterministic *virtual*
   work-unit count (`SearchBudget::NODES_PER_VIRTUAL_MS = 900`), and `<= 0` disables the limit. A
   20480 ms budget is still bounded. The budget being work-units, not wall clock, is also why these
   runs reproduce under any machine load.
3. **The d5 case OMITS the depth key so `value_play` owns the depth — so the two arms can run at
   DIFFERENT depths.** Here: baseline d5, value-model arm d6. **`explain_game.py` pins
   `--depth <case depth> --ignore-play-profile`**, which bypasses `value_play` and silently diagnoses
   a configuration the case never ran. Reproduce a d5 case by OMITTING `--depth` and passing only
   `--budget-ms`.
4. **`explain_game.py --old-bin` resolves the deck from the CURRENT tree.** Fine for a code change,
   useless when the change is a deck-sibling data file: the old binary still picks up the new
   `value.json` and reports "no change". Use a deck-copy A/B instead.
5. **`--ignore-play-profile` does not disable the value model.** It bypasses the play config only.
   Do not assume a d0/d3 case is model-free — check whether the play digests actually differ.

Also worth knowing: **`value_play` may live in `<deck>.value.json` OR in the profile** —
`MulliganProfileIO.h` reads `value_play` from the model's meta first, then the top level. Grepping
only the profile will tell you a deck has no play config when it does.

## The overnight tier: what is left to explain

All 12 overnight failures are goblins; the other eight decks are green (their overnight GT was
re-accepted as recently as `cb806c8` / `e97c85a`). Four commits have landed since `9462479`:

| commit | behavioural? | reaches d0/d3? |
|---|---|---|
| `8a3bbb1` value model | yes | **yes at d3** (value leaf), not at d0 |
| `ec70359` goblins search-perf | yes | yes |
| `be7bc02` move cold helpers to a .cpp | no | — |
| `57069d9` hook numbers → names | no | — |

- **d0 ×4, ~−0.065 (large improvement).** Cannot be the model (no search ⇒ no leaf). Attributed to
  `ec70359`'s new `GoblinsProvider` (defers creature-sac value outlets to second main, haste-gates
  Skirk's sac-for-mana) — a d0-visible executor change. Note `ec70359` measured itself as
  quality-neutral (±0.0025 held-out) on the *deep rollout* arm, not at d0.
- **d3 ×4, ~+0.003.** The model accounts for only +0.0004 of this; the rest is `ec70359`.
- **d5 ×4, mixed.** Model (−0.00136) plus `ec70359`.

`ec70359` isolated with worktrees at `ec70359` and `ec70359^`, both built with `build.sh`, the
**no-model deck copy on both** so only the code differs, seeds spaced 1000
(`test/logs/goblins_ec70359_isolation/`, 100,000 distinct games per depth):

| depth | play lines differ | better/worse | win→unwon | unwon→win | mean delta | |
|---|---|---|---|---|---|---|
| **d0** | 68.25% | 6891 / 1739 | 10 | **255** | **−0.06086** ± 0.00113 | −53.9σ |
| **d3** | 42.63% | 713 / 891 | **0** | 4 | **+0.00177** ± 0.00040 | +4.4σ |

The d0 isolation (−0.0609) matches the GT movement (−0.0659) almost exactly, which is what makes the
attribution credible rather than coincidental. Accounting for the whole tier:

| depth | GT delta | = attribution |
|---|---|---|
| d0 | −0.0659 | `ec70359` −0.06086 |
| d3 | +0.0030 | `ec70359` +0.00177 + model +0.00041 = +0.0022 |
| d5 | +0.0030 | model −0.00136, so `ec70359` ≈ +0.0044 (isolation run: `test/logs/goblins_ec70359_d5/`) |

If that d5 figure holds, **`ec70359` exceeds the ±0.0025 neutrality bar it claimed for itself** — its
own validation measured the deep *rollout* arm, not the d5 play config.

## The unmeasured cell — RESOLVED upstream 2026-08-02

Fixed by `70515df` (set `value_trust_depth: 6`, fold the real V6–8/H4–5 matrix into the profile),
`72f87cb` (a completeness guard that refuses to write a truncated ladder) and `fc1dc98` (the bug class
written up as `value-leaf-ladder-truncation.md`). **The suspicion was right: `hc*[6]` measured as 4,
not 9** — the sentinel was padding from a truncated ladder, and the real threshold is reachable
(`hcommitted <= depth = 6`). Effect in play: d6 escalations **47 → 0**, total redos **60 → 18**.

The guard described at the end of this section is **dropped**, and the reasoning is worth keeping:
once every deck is configured correctly, `value_trust_depth` already stops the escalation wherever a
take threshold exceeds the reachable depth — the guard fires nowhere in the fleet. It was only ever a
backstop against a *misconfigured* deck. `72f87cb` fixes that at the source by refusing to emit the bad
table, which is the better place: don't work around bad data in the engine, don't ship it.

**Caveat worth carrying: the deciding margins are inside the measurement's noise.** The trust test is
`V_d − h_conv <= tol`, and `V6 − h_conv = 0.0020` against `tol = 0.002` — it passes by exactly zero
margin. `hc*[6] = 4` rests on `V6 − H4 = 0.0015`. Per-game loss-penalized stdev on this deck is
**0.9937** (measured over 100,000 d6 games), so a 3000-game cell carries s.e. ≈ **0.018** unpaired, or
~0.002–0.007 if the arms are seed-paired. Both margins are 0.3σ–1.1σ. The deep end of the ladder
(H3/H4/H5 = 4.3988/4.3952/4.3947, V6/V7/V8 = 4.3967/4.3947/4.3947) is flat to within error.

That does **not** flip the decision — escalating at committed 6 would gain ~0.0015 LP and only ~45% of
those escalations reach h≥4, so the expected gain is ~0.0007 LP against 21.8% of the instruction count.
Trusting the leaf is the right trade under either reading. But do not treat the 0.002 margin as solid,
and note two estimator biases: `h_conv = min(heuristic_lp)` takes a minimum over noisy cells so it is
biased low, which makes the trust test *conservative* (trust was earned against a headwind); whereas
`hc* = min{hc : H_hc < V_c}` is a first-crossing statistic, biased low, which makes the engine take the
heuristic slightly too eagerly.

### Original diagnosis (kept — it is how the gap was found)

Goblins' `value_fallback_crossover` reads `take_heuristic_at_hdepth = [1,1,1,1,3,9,9,9]`, and the 9 at
committed 6 looks like "trust the leaf at 6". **It is not.** From the deck's own `value_leaf_table`:

```
hdepths = [1, 2, 3]           heuristic measured only to depth 3
vdepths = [1, 2, 3, 4, 5]     value leaf measured only to 5 -- V_6 NEVER measured
h_conv_depth_cap = 5
derivation: "Deep cells d>cap extend the table+crossover for the anchor but do NOT move the
             escalation gate; a trust_depth > search_depth is equivalent to UNSET in play."
```

`hc*[c] = min{hc : H_hc < V_c}` can only return 1..3 or the sentinel, because maxH is 3. The 9 at
`c=6` falls out of the **absence** of V_6, not from the leaf being strong. The table was built when
Goblins was a d5 deck; `8a3bbb1` moved it to `target_depth: 6` without regenerating it, so the deck's
**modal** committed depth (121 of 288 probes in a 60-game sample) is decided by padding.

Audit of every deck — measured depth vs played depth:

| deck | plays at | V measured to | H measured to | trust | covered? |
|---|---|---|---|---|---|
| **burn** | 6 | 8 | 8 | 6 | yes — the template Goblins should match |
| Knights / slivers / TH | 5 | 8 | 8 | 5 / 5 / — | yes |
| Anti-Lifegain | 5 | 7 | 6 | — | yes |
| Auras / Dragonstorm / Hinata2 | 5 | 5 | 3 | 5 / — / — | yes |
| **Goblins** | **6** | **5** | **3** | **—** | **NO** |

Goblins is the only deck that plays deeper than it was measured. Burn is the exact structural twin —
also d6, also a 9 at `c=6` — but burn's 9 is *earned*: measured to V8/H8, so no heuristic depth up to
8 beats its leaf at committed 6, and its `value_trust_depth: 6` is justified.

**The cost of the gap.** Because `value_trust_depth` is absent, `escalate_below` falls through to
`m_lookahead_depth + 1 = 7`, so a line committed at the full depth 6 still escalates — and then
`taken = (hcommitted >= 9)` is *provably* false, since `pred_max = min(depth, committed)` caps
`hcommitted` at 6. The engine searches and discards, every time. Measured: **47 of 60 escalations**
commit at 6, and callgrind puts the discarded work at **21.8% of all instructions**
(199.94e9 → 156.40e9 on a 60-game d6 job).

So today's behaviour is the one option that cannot be right: pay 21.8% and throw the answer away on
the authority of a cell that was never measured.

**Do not "fix" this by adding `value_trust_depth: 6`, and do not skip the escalation** — both cement
the unmeasured assumption. The repair is to regenerate the table at the depth the deck now plays
(V6–V8, H past 3) so `hc*[6]` is evidence. Generator: `scripts/attic/valueleaf_depth_matrix.py`
(retired in `85fe904`; check it over before use).

**Regenerate d1–d5 alongside the deep cells, and check the anchor.** The antilife precedent
(`antilife-valueleaf-deep-cells-overnight.md`) is that extending a table revealed the *existing*
cells were stale — the fresh sweep did not reproduce them because `bf89675` (reshuffle clairvoyance)
had moved the deck ~0.5 LP underneath them. Goblins' cells were built at `games=400, seeds=[8008,
9009]` on an older engine, and `ec70359` alone moves d0 by −0.061. Appending without re-anchoring
would silently mix two engine states.

An unadopted guard for this exists in the working tree but is **deliberately uncommitted**: it derives
the trust depth from the table (one `take_threshold` helper serving both the pre-check and the take
decision) behind `MTG_ESC_SKIP_UNTAKEABLE`, default off, verified byte-identical on smoke (27) and
regression (45) and confirmed to fire (redos 60 → 13, d6 escalations 47 → 0). It is held because its
correctness depends entirely on which way `hc*[6]` lands once measured.

## Open

- **The affordability gap.** Even with the table fixed, escalations are paid for and then discarded
  when the search cannot afford the threshold depth. Under the pre-fix config only **3 of 60**
  escalations were actually taken: 47 were structurally unusable (the padding cell) and 10 more were
  at committed 5, where the threshold is H3 but the escalation reached only H1/H2 every time. Skipping
  those needs a *predicted* affordability, and that is not reliable today for three reasons:
  (1) the shadow-ladder audit `MTG_ESC_PREDICT_AUDIT` only instruments the **research** branch
  (`s_esc_predict && (tt == nullptr || s_esc_predict_warm)`), not the `eff_single_deck` path every deck
  runs — the shipped predictor has no measured accuracy; (2) `escalation_r` is **absent on all nine
  decks**, so the cost model falls back to the deck-agnostic prior `R = 120.0`, and the field exists
  precisely to hold a calibrated value; (3) the error is asymmetric — today a bad prediction picks the
  wrong pass depth and the overrun-abort recovers, but gating on it would silently discard a takeable
  escalation, which the code itself already labels `LOSSY`. Order of work: extend the audit to the
  adopted branch, calibrate `escalation_r` per deck, measure the lossy rate, and only then gate.
- **`escalation_cap` is inert fleet-wide.** It is documented as the CONVERGENCE cap ("heuristic gains
  ~0 past it", `clamp(cap, 1, depth)`, "never search deeper") but is set equal to `target_depth` on all
  nine decks, so it never binds. Its live function is as a *switch*: `escalation_cap > 0` is what
  selects `eff_single_deck`, the single-pass predicted-affordable path. Worth either setting it to the
  measured convergence depth or renaming it for what it does.
- Whether the d3 `+0.00041` from the value leaf is worth suppressing (e.g. raising the leaf's
  minimum depth so it does not fire below the depth it was validated at). It does not affect the
  shipped configuration.
- `ec70359` at d5 (≈ +0.0044 if the pattern holds) against its claimed ±0.0025 neutrality bar.
