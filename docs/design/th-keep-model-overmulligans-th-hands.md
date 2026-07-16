# TH keep model over-mulligans Reliquary-Tower + TH hands (INVESTIGATED 2026-07-15)

## 2026-07-16 (session 4) — RESOLVED + ADOPTED: RT-only keep-floor (`MTG_TH_KEEPFLOOR`, default ON)

The over-mull is fixed by a **keep-floor**: a `DecisionProvider::KeepFloor` hook (new Hook 26; base returns
`Undecided` → every non-TH deck byte-identical) that `AIEngine::HandleMulligan` consults before the exhaustive
keep table. `TreasureHuntProvider::KeepFloor` **force-keeps a castable Treasure Hunt hand ({1}{U} → ≥2 lands
incl. a blue source) that also holds a Reliquary Tower**, at the initial 7 only. Behind `MTG_TH_KEEPFLOOR`
(default **ON**; `=0` → legacy). Adopted; TH GT (smoke/regression/overnight) rebaselined.

**Scope = RT-only, decided by data.** Measured keep vs the table's own RECURSIVE mulligan (the thing the floor
replaces), under NON-CLAIRVOYANT blind play (the real-opponent proxy), on exactly the hands the table mulls:

| composition (table-mulled) | n | delta avg9 (neg = keep better) | verdict |
|---|---|---|---|
| **TH + Reliquary Tower** | 137 | **−0.737 (t=−4.17)** | strong, significant keep — the genuine over-mull. **ADOPT.** |
| TH + Saprazzan Skerry | 168 | +0.232 (t=+1.40) | mildly keep-*worse*, not significant. **DROPPED.** |
| TH + Skerry, ≥2 TH | 0 | — | table **never mulls** these (already keeps them) → a Skerry clause would only ever touch Skerry+1TH |
| 1-TH-plain (control) | 1096 | +0.276 (t=+4.55) | correctly keep-worse (harness discriminates) |

Why RT and not Skerry: RT's payoff ("no maximum hand size" → stops the flood-discards) is **passive and
blind-robust** — it doesn't need clairvoyant draw-knowledge or a particular matchup, which is what a *hard*
force-keep (no search deviation) requires. Skerry's clause only ever changed Skerry+1TH hands (the table
already keeps every Skerry+2TH), and those are ~correct to mull. Earlier "Skerry/2TH strongly keep" numbers
(−0.8…−1.2) were measured vs a WEAK `--force-mulligan "1:"` baseline (mull once to a random 6), which answered
the wrong question (keep vs random-6, not keep vs the table's smart recursive mull).

All three metrics agree keep-better with **0 searched win→loss**: NC −0.737; goldfish regression neutral-to-+
(3/5 cases better); goldfish overnight held-out clearly + (searched earlier 52 vs later 24, mean ≈ −0.006).
Every rebaselined game is a TH+RT seven the table used to mull (structural: the floor's only behavior).

**USER RESERVATION (revisit Skerry):** the user (expert blind player) still suspects TH+Skerry is a keep; the
data doesn't clear the hard-override bar vs the table's smart mull, so we took the safe RT-only bet. Not
refuted — to revisit cleanly, build an **exact-hand recursive-mull eval**: construct a named hand, NC-rollout
`V_keep(hand)` vs `V_mull(deck, recursive)` directly (the current force-keep-`0:`-vs-force-mull-`1:` harness
uses a pessimistic single-mull baseline that inflates keep deltas). Full state: memory
`th-keepfloor-inflight-2026-07-16`.


## 2026-07-15 (session 2) CORRECTION — the "over-mull" is a NEAR-TIE, not a confirmed loss

A game-level keep-vs-mull A/B on the EXACT mulled hands (force-keep at mull 0 vs let the table mull,
same seed/gi, real depth-5/20ms play) OVERTURNS the earlier "confirmed −0.0027 over-mull" and the
"Skerry land-timing play-gap" framing below. Both were small-sample artifacts.

| sample (mulled Skerry+TH, no RT) | n | keep mean | mull mean | delta (neg=keep better) |
|---|---|---|---|---|
| first batch (seeds 1–1558) | 68 | 4.073 | 4.162 | −0.088 |
| **fresh batch (seeds 5001–13000)** | **120** | **4.200** | **4.200** | **0.000** |
| pooled | 188 | 4.154 | 4.186 | −0.032 |

The larger fresh sample is a **dead tie** (keep-faster 40 / mull-faster 43 / tie 37). So keeping vs
mulling these hands is worth **≈0 turns** — they are genuine ~T4.2 near-ties sitting right on the
keep/mull threshold. The generator keeps ~83% (322/390 in a 4000-game scan) and mulls ~17% (68) purely
by which side the **R≈40 per-cell rollout variance** lands on; since the truth is a tie, the mulled 17%
are not actually worse off. The earlier −0.088 (and the even-earlier broad −0.0027) were OUR
small-sample bad luck (a few extra-slow mull cascades). **Verdict: not a costly over-mull, not a play
bug, not random-noise-with-a-sign — a genuine near-tie the generator resolves arbitrarily.**

**The one real, if minor, play gap** (the user's "discard well" concern): the cleanup discard selector
`SelectCleanupDiscardIndex` (src/core/SpellEffects.h:83) protects the payoffs (highest-MV / lands-as-ammo
via `TreasureHuntProvider::DiscardLandsFirst`) but treats a **Reliquary Tower as just another land** —
it sheds it by hand order with NO protection. So a flooding hand can DISCARD a Reliquary Tower (the one
card whose no-max-hand-size would STOP all further discards) instead of PLAYING it. Observed in 6/68
force-kept hands (s1558 discards an RT at T4 cleanup). Rare, ~0 aggregate impact, but a clean, defensible
fix: in lands-as-ammo mode, never shed a `no_max_hand_size` land before a plain land — better, play it.

Land-deferral machinery (defer land → cast TH → play a TH-revealed Reliquary as the drop) IS implemented
and runs at depth>0 for BOTH real play and the keep-scoring rollout (`EnumeratePlansWithLand` defer branch
`TurnSolver.cpp:5253`; `PostDrawKeepLandName` `DecisionProviders.cpp:1039`, gated on hand>7 flooding). The
keep-scoring rollout is NOT a greedy d0 rollout — it's the same depth-5/20ms search real play uses, so
there is no train/serve fidelity gap here. Everything below this line is the ORIGINAL (now-superseded)
session-1 writeup; keep for history.

## 2026-07-16 (session 3) — strict flood gate + forced-defer (UNCOMMITTED; avg9 verdict: not there yet)

Prototyped the strict flood-engine sequencing behind `MTG_TH_STRICT_FLOOD` (default off, byte-identical;
non-TH decks unaffected). Two parts: (1) `ShouldCastDrawEngine` drops the spent-drop "dig anyway" clause
(don't cast a payoff-less Treasure Hunt once the drop is spent — only with the drop open, to play a drawn
Reliquary); (2) `TreasureHuntProvider::ForceDeferLandForFlood` (provider hook; engine only calls it) forces
the land drop deferred on a flood-engine turn with no outlet. **Measured on avg9 (loss=9), the metric:**

- **Gate alone**: clairvoyant avg9 +0.14 worse, but **NC (non-clairvoyant) neutral (+0.006)** → the
  clairvoyant hit is ~95% fake known-draw speed. Fixes the RT-discard fidelity misplay (s1558).
- **Gate + forced-defer**: recovers the win→loss cases but clairvoyant avg9 **+0.20 — worse than gate
  alone** via OVER-DEFERRAL (forces defer even on hold-the-Hunt-and-develop turns, skipping the land drop).
  So the blunt force-defer is the **wrong direction on avg9**.

The correct fix is the valuation hardening already scoped in `th-reliquary-defer-gi627.md`
("harden the valuation, don't pre-decide"): make the rollout value defer→play-drawn-Reliquary→keep-flood so
the search COMMITS it without a blunt force-defer, plus (lethal-conditioned) don't-re-dig-when-Throes-is-lethal.
A tempo-neutral alternative to force-defer is to also play the deferred land at end of turn when no Hunt is
cast. Full state + next steps: memory `th-strict-flood-gate-2026-07-16`. Nothing committed; profile/GT untouched.

---

**Status (session 1, SUPERSEDED above):** investigated with force-keep A/Bs (real searched play, 8 seeds × d3/d5 × 3000g, avg9).
Result: the broad "over-mulls all TH hands" hypothesis is REFUTED, but a **narrow, real over-mull of
Reliquary-Tower + TH hands is CONFIRMED**, plus a likely **land-drop-timing play gap** around Saprazzan
Skerry. Nothing changed/committed yet — all tests were on `/tmp` profile copies; the committed TH profile
is untouched. The mm6 TH profile stays adopted (net −0.15 vs prior, 8/8).

## RESOLUTION — force-keep A/B verdicts (the decisive tests)

Method: patch the committed profile to force `keep[0]=1` on a target subset, A/B the patched vs current
profile with REAL searched play (both arms `MTG_EXHAUSTIVE_PROFILE=none --profile <arm>`,
`MTG_EXHAUSTIVE_BOTTOM=1`, value.json attached; only the keep table differs). Metric = avg9 (loss=9).
Negative delta = force-keep BETTER = the current mulls are wrong.

| subset force-kept | Δavg9 | seeds better | game flips | verdict |
|---|---|---|---|---|
| **all** castable TH hands (TH≥1, land≥2; 4614 comps) | **+0.010** | 0/8 | +32 wins but slower | current mulls RIGHT |
| high-dig (TH≥2 or TH+Throes; 1401) | +0.003 | 2/8 | 0 flips | near-tie, mull right |
| **Reliquary Tower + TH (198)** | **−0.0027** | **7/8** | **+8 wins, 0 lost** | **OVER-MULL confirmed → keep** |
| Saprazzan Skerry + TH (223) | −0.0000 | 3–4/8 | 0 flips | NEUTRAL (see play-gap below) |

**So the model is right to mull the no-enabler flood hands (63% of mulled 1-TH hands lack any
Reliquary Tower / Skerry / Sandstone Needle), but WRONG to mull the ~198 Reliquary-Tower + TH hands.**
Those are castable-but-slow flood hands (tapped/redundant/some no-red mana bases) that Reliquary Tower
makes keepable — you hold the TH flood instead of discarding it. The **greedy keep-rollout**
(`RolloutKeepWinTurn`, confirmed depth-independent: score identical at `MTG_EQUIV_DEPTH` 5 vs 8)
under-rates the Reliquary-hold-then-dig line and tips these marginal hands to mull.

## The Saprazzan Skerry play gap (hypothesis from the NEUTRAL result)

User's line: Skerry taps for `{U}{U}` alone → cast T2 Treasure Hunt off Skerry **while holding the land
drop**; then play the Reliquary Tower that TH rakes into hand *as that turn's land*, gaining no-max-hand-
size the same turn. This is the bridge to Reliquary Tower when RT isn't in the opener, and (per the user)
speeds the deck up a lot. Force-keeping Skerry+TH was NEUTRAL (0 game flips) — if the engine played that
line, keeping would have helped like RT+TH. So the engine likely **does not sequence "TH-off-Skerry →
defer land drop → play the drawn Reliquary Tower"** (plays its land on curve, casts TH off two lands).
Fixing that land-timing would speed up EVERY Reliquary-less game, not just a mulligan — higher value than
the keep-floor. **NEEDS VERIFICATION**: trace the engine's play of a Skerry+TH hand (does it hold the land
for a TH'd Reliquary Tower?).

## Recommended next steps (ranked)

1. **Verify the Skerry land-timing gap** (trace a Skerry+TH playout). If confirmed, fix the land-drop
   sequencing — the bigger, deck-wide win.
2. **RT+TH keep-floor** (cheap, already-measured +): force `keep[0]=1` when the hand has Reliquary Tower
   + a castable Treasure Hunt. Confirm on a larger sample first (−0.0027 is small); implement in the TH
   archetype provider, not the root. (Once the Skerry play-gap is fixed, RE-MEASURE — the floor gain may
   shift.)
3. **Root fix**: regenerate with a searched (or better-sequenced) keep-rollout so the under-rating goes
   away generally, not just for RT.

Reproduction harness (untracked, `/tmp`): decompress committed profile → `/tmp/th_current.profile.json`;
python-patch `keep[0]=keep[1]=1` on the target comps → `/tmp/th_forcekeep*.profile.json`; A/B via a
copy of the mm6 run_arm loop. Buckets: b0=Ferrous/Frostboil/Island/Steam, b4=Land's Edge, b6=Reliquary
Tower, b9=Saprazzan Skerry, b11=Throes, b13=Treasure Hunt.

---

## (original note, partly superseded above) Observation

## Observation

Inspecting the adopted TH exhaustive keep table (`decks/treasure_hunt/*.keepmodel.exhaustive*`,
mm6, R=41, `MTG_EQUIV_DEPTH=5`), the model mulligans many hands that contain **Treasure Hunt**:

- Of 21,716 TH-containing size-7 comps, ~20% (4,307) have `keep[0]=0` (mull the opener).
- 97% of those are 5–6 land "lands + Treasure Hunt, no payoff" hands; it even mulls every one
  of the 145 ≤4-land TH hands (e.g. "3 lands + 4 Treasure Hunt").
- Keep rate by dig engine: **has TH 80% / Throes-only 25% / no dig 0%** (never keeps a no-dig
  hand — correct). Throes-keeps favor an accel/storage land (Sandstone Needle/Saprazzan Skerry)
  2494:118 — the model already encodes "Throes is slow without acceleration."

The user's expectation: with **only 7 nonlands in the 60-card deck** (4 Treasure Hunt, 2 Land's
Edge, 1 Throes), lands are the win-condition *fuel* (discarded to Land's Edge for 2 each), not
flood. A single Treasure Hunt into a near-all-land library draws a huge pile of lands → Land's
Edge burns them out. So a hand with a few lands + a dig spell should almost always be a keep.

## Why it is NOT a bug

- **Treasure Hunt is modeled correctly** (`EffectHandler::ResolveDrawUntilNonland`): reveals
  from the top until a nonland *or the library empties*, puts all revealed (incl. the nonland)
  into hand via a raw library-pop — **not** the game "draw" action — so emptying the library does
  **not** deck you (the `while(!library.empty())` loop just ends). Decking only happens on the
  next draw step from an empty library, which IS modeled as a loss (CR 104.3c, `Library.h` /
  `EffectHandler.cpp:361`). So multi-TH hands are not mis-scored by a phantom deck-out.
- The keep decision is a backward-induction over the hypergeometric hand distribution
  (`ComputeKeepPolicy` in `ExhaustiveKeep.cpp`): keep iff the hand's keep-value ≤ the *expected*
  value of mulliganing (averaged over all fresh hands). NB the raw sidecar's two per-hand numbers
  are `V[on_the_draw, on_the_play]` — BOTH keep-values — not keep-vs-mull; the mull-value is a
  distribution average, so you cannot read the decision off a single hand's two arms.
- The mulled TH hands have keep-value ≈ **4.1** (≈ deck average, a turn-4 goldfish). They mull
  because the average fresh hand scores marginally lower — genuine **near-ties**, not blunders.

## The refinement hypothesis

The keep-value is produced by the keep-rollout at **depth 5 / 20 ms** (`ExhaustiveKeepConfig`).
A shallow rollout may not find the optimal single-Treasure-Hunt → Land's Edge → discard-lands
burn sequence, so it likely **underrates the true ceiling** of "lands + TH" hands (nearer turn 3
with perfect play) → a mild, systematic over-mulligan of castable Treasure-Hunt hands.

## Candidate fixes (for a future TH regen, on a frozen commit)

1. **Regenerate TH at higher `MTG_EQUIV_DEPTH`** (e.g. 7–8) and compare the keep table + the
   NEW-vs-OLD avg9 A/B (`test/mm6_newvsold_ab.sh`). If deeper rollouts raise the keep rate on
   "has TH" hands AND improve avg9, the over-mulligan was a depth artifact.
2. **Keep-floor for "has TH" hands**: force `keep[0]=1` whenever the hand has ≥1 land and ≥1
   Treasure Hunt (castable), then A/B vs the current table. Cheap to test; if win-or-tie, it
   confirms the hands are keeps and gives a simple correction without a full regen.
3. **Decisive single-hand probe**: `mtg <deck> --profile <prof> --eval-hand "Island;Island;Treasure Hunt;..."`
   at depth 5 vs a higher depth to see whether the keep-value drops (better) with more search.

## Verification method used (for reproducibility)

Recover the OLD profile from the parent commit, run a game's `gi` under OLD vs NEW via
`--profile <exh> MTG_EXHAUSTIVE_PROFILE=none --seed base+gi --games 1 --log-dir`, and read
`mulliganSequence` (kept `attempt` / effective hand size). This is drift-proof; the inline
`explain_game` diff is blind to a profile-only change (old profile off disk).

## 2026-07-16 (session 4) — strict flood GATE = the correct force-defer; play-side, avg9-validated

The play-side thread (draw-and-discard a Reliquary from a spent-drop Treasure Hunt) is fixed by a
**gate-only** change behind `MTG_TH_STRICT_FLOOD` (default OFF, byte-identical). `ShouldCastDrawEngine`
drops its spent-drop "dig anyway" clause; because that gate is consulted *inside* the per-land
enumeration (`EnumeratePlansWithLand`→`EnumeratePlans`), once a land is played the drop is spent and the
gate refuses TH/Throes — so the only way to cast the flood engine with no outlet is the **defer** plan
(drop still open), and the post-draw breakpoint re-solve then lets the **search** decide the after-play
(play the drawn Reliquary as the drop, play a hand land, cast Land's Edge for the win, or nothing). That
is the whole of the user's rule "don't play a land BEFORE TH/Throes; the after-play is a search decision."
The earlier `ForceDeferLandForFlood` hook (emit ONLY the defer plan) was an OVER-REACH that also deleted
the develop / Land's-Edge / play-the-land lines and skipped the drop on hold turns (avg9 +0.20); it was
**removed** this session. Deck logic lives in the provider (`ShouldCastDrawEngine`), engine untouched.

**avg9 validation (metric = avg turn-to-win, LOSS=9; win/loss discarded).**

| play mode | games/arm | gate − legacy avg9 | note |
|---|---|---|---|
| clairvoyant (harness/GT) | 16000 (d3+d5×4 seeds) | **+0.124 (worse)** | 229 games/… win exactly 1 turn later; win→loss = 1 in 8000 = ~zero |
| non-clairvoyant (real) | 2000 (8 seeds, K8/D2) | **−0.034 (better)** | 6/8 seeds favour gate |

**Attribution of the 229 clairvoyant-slower games** (NC both arms, same gi): clairvoyant delta **+1.044**
collapses to NC **+0.183**; **66% of the slowdowns vanish under blind play** (128 tie, 24 gate faster), 34%
(77) keep a small real cost (+1 on 51, +2 on 17, +3 on 3) that is net-offset by fidelity gains elsewhere.
Mechanism (gi=3, logged): legacy T2 spends the drop + casts TH, discards the revealed Reliquary Tower,
wins T4 only because it clairvoyantly knows the T4 Throes→Land's-Edge kill; the gate T3 defers → casts TH →
plays the drawn Reliquary (0 discards), wins T5 — the blind-correct line. So the clairvoyant +0.124 is fake
known-draw speed the gate correctly refuses; on the real (NC) metric the gate is neutral-to-slightly-better.

**Adoption** (user's call): default-on rebaselines the CLAIRVOYANT TH GT +0.124 avg9 slower (fingerprint =
`games_won/avg_win_turn`) — correct but reads as a regression to a clairvoyant-harness observer. Options:
(a) adopt default-on + rebaseline TH GT (document the number is fake); (b) keep opt-in until the deferred
exe-avg9-reporting change lands, then flip on; (c) drop. Nothing committed. Artifacts: `test/logs/th_strict_ab/`.
