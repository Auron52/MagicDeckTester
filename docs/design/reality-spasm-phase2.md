# Reality Spasm phase 2 — the untap model, what it still launders, and the rework plan

**Status:** BUILT + MEASURED (2026-09-01, commit 53fa08eb) — see §7 for the results. The lever is
`MTG_SPASM_UNTAP_LITERAL`, **default OFF**; flipping it (with the GT rebaseline and the sidecar
question) is the USER's adoption call. Originally designed 2026-08-30. This is the doc
`unpriced-prepay-rows.md` links as "cause 2's blocker". Read this before touching the Spasm model
or spending any effort on the unpriced-rows "production table audit" (§3 kills that step).

## 1. Current state — CORRECTING two stale claims

Two circulating descriptions of Reality Spasm are out of date:

- **"Reality Spasm is not cast (a dead card)"** (the `cards.json` bracket note). STALE. The
  autonomous search casts it constantly — it is the core of hinata's kill (Spasm → refloat →
  Soulfire/next Spasm → Crackle; see `hinata-gi61-spasm-cast-order.md` §4, and §3's sampled
  logs below where turns cast up to FOUR Spasms). The emission path is
  `EnumeratePlans`' `untap_x_mana_sources` branch (TurnSolver ~6628) gated on
  `ShouldEmitUntapRitual`; X = `ManaSourceCount` (all sources), the {X} discounted by Hinata.
- **"the untap mode is modelled as floating X WILD mana"** (`unpriced-prepay-rows.md` cause 2,
  and the old bracket note). PARTIALLY STALE. Since the ritual true-colours fix, the live path
  floats a **coloured** pool: `RitualRefloatPool` (SpellEffects.h ~7461) picks the top-X
  sources by per-tap output and floats their `EffectiveProduces` colours.
  `MTG_LEGACY_RITUAL_WILD=1` restores the old wild lump (A/B hatch; never ship it).

So the model is: *"float the coloured output of your best X mana sources"*. That is much closer
to the card than the wild lump was — but it is still a float, and two real defects remain.

## 2. The two remaining defects (read the code, both confirmed 2026-08-30)

**(a) Tapped-ness is ignored — magnitude over-credit.** `RitualRefloatPool` (and
`ManaSourceCount`, which sizes X) select from ALL controlled sources, tapped or not. The real
card untaps *tapped* targets; an untapped target gains nothing. So whenever fewer than X
sources are tapped at resolution, the model floats mana the rules would not grant. Enum and
apply agree (both use the same pool/amount), so the engine plays a *self-consistent but
generous* game — no enum/apply mismatch, just free mana.

**(b) Choice-of-N floats as WILD — colour over-credit.** In `RitualRefloatPool`, a source with
`prod.size() > 1` and `amt == 1` (any dual/triome land) falls to the `pool.wild += amt` branch.
Wild pays ANY pip; the real choice is one of that source's N colours. For N < 5 this is the
same colour-laundering channel the prepay true-colours work closed elsewhere, in miniature.
(The `amt > 1` multi-colour branch has a milder version: one-of-each + wild remainder.)

Note (b) exists because `ManaPool` has no "one of {R,G}" constrained unit — and that is the
argument for fixing (a)+(b) by **de-abstracting to a literal untap** rather than teaching pools
constrained units: untapped real sources re-tap through the normal payment machinery, which
already prices colour choice exactly.

## 3. The stake, measured: the float IS the hinata unpriced mass

The 2026-08-29 unpriced-rows split (see that doc) put 111 of 183 UNPRICED rows in
"short-count" and recommended auditing the pricing model's production table. **That
recommendation is now dead.** Sampled 2026-08-30: re-ran the legality control for 12 hinata
short-count rows and read the short turn's casts from the logs — **12 of 12 short-count turns
cast Reality Spasm** (several cast 2–4 Spasms plus Irencrag Feat, ending in Crackle). A
float-funded turn spends more than its sources produce, which is exactly the ledger's
"short on raw count" abstention — so cause 3's bulk is cause 2 wearing its label, and the
`{X}` bucket (77 rows) is largely Spasm's own cost string. The Spasm model is not one of
three causes; it is essentially THE cause of hinata's 149 unpriced rows.

## 4. The rework (phase 2 proper)

Replace the float with a **literal untap**, resolution-side:

1. At resolution, choose `min(X, #tapped mana sources)` TAPPED sources — same selection
   heuristic as today (highest per-tap output first, battlefield order tiebreak), restricted
   to the tapped subset — and UNTAP them (a state change, no float). Re-tapping them later in
   the turn goes through the normal payment machinery: colours exact, no wild, chains real
   (Spasm → retap → Spasm again is then a true sequence, which it already effectively is —
   float-per-cycle and untap-retap-per-cycle have equal totals WHEN everything is tapped;
   they differ exactly in the defect-(a) cases).
2. Enumeration credit: the emitted action's `ritual_float` becomes a *projection* of
   `min(X, tapped-at-resolution)` sources' output. Conservative first cut: project from the
   sources the plan's own payment will have tapped by the Spasm cast (the prepay machinery
   already knows the payment); the rollout realises the truth either way, so a slightly-off
   projection costs plan-ranking quality, not correctness.
3. Log the untap (which permanents) in the game log, so `prepay_recheck`'s ledger can price
   those turns: a source that untaps mid-turn legitimately produces twice, and the ledger can
   count it per tap instead of abstaining. This is what retires the hinata unpriced mass.
4. The `HinataRitualNetBonus` planner credit and `Solve::consider`'s per-subset gross credit
   follow the same projection (scalar totals; they only rank).

**Expected direction of the measurement — say it before running it:** today's model is
GENEROUS, so an honest model should make hinata slower-or-equal on some games. Per the prepay
doctrine, those games getting worse is the fix *working* (their old wins were part-funded by
mana the rules never granted); the adjudication tool is the ledger itself, which afterwards
can finally price the turns. Do NOT treat hinata GT regressions as failures to recover.

## 5. Verification plan

- Identity-routing spot checks per `crackle-reality-spasm-overgeneration.md`'s method (that
  doc's enum/apply mismatch should be re-checked first — it predates true-colours and may
  already be stale; its combined RS+Crackle edge collapses into this rework either way).
- Hinata train cells A/B (flag-gated during measurement, e.g. `MTG_SPASM_UNTAP_LITERAL`),
  full suite for byte-inertness outside hinata (only hinata + dragonstorm's Irencrag share
  the ritual machinery; Irencrag is a fixed burst and untouched by the untap change).
- On adoption: single tier runs + GT rebaseline on the frozen commit, then RE-RUN the
  unpriced split — success criterion: hinata's UNPRICED mass collapses into LEGAL/LAUNDERED.

## 6. Sizing

Engine change in hinata's core kill path + a log-format addition + ledger support: a half-day
of work plus the measurement battery. NOT a quick item — parked here until the box and the
queue allow it.

## 7. BUILT + MEASURED (2026-09-01, commit 53fa08eb) — awaiting the USER's adoption call

**What was built** (all flag-gated on `MTG_SPASM_UNTAP_LITERAL`, default OFF):
- The literal untap lives in `ApplyRitualFloat`'s untap branch (`RitualUntapSources`,
  SpellEffects.h): up to X TAPPED sources untap, float-model selection rule restricted to the
  tapped subset. ONE shared resolution function ⇒ executor / rollout apply / plan apply /
  `SubsetPayableSequential` are lockstep **by construction** — no per-site mirroring was needed,
  and the sequential-feasibility gate (MTG_EXEC_FEAS) prices Spasm chains exactly for free.
- The enumeration credit (`a.ritual_float`) was deliberately LEFT as the float formula = an
  **optimistic upper bound** (§4's "conservative first cut" landed on the other side: the
  rollout's TapForCostDirect failure path drops an over-credited cast and the plan scores
  honestly, so the bound costs plan-ranking quality only, never phantom mana).
- A heurarm slot (`SPASM_UNTAP_LITERAL`), so ONE pooled batch carries both A/B arms.
- The `UNTAP_SOURCES` game-log event (g_reveal_logger-gated ⇒ real resolutions only) records
  exactly which sources untapped, and `prepay_recheck`'s `_turn_ledger` consumes it as an
  **exact** credit (no `soft` note — a balancing turn proves LEGAL), superseding the generous
  chosenX bound whenever events are present.

**Flag-off is byte-identical everywhere** (hinata d0 1000g + d3 150g vs the pre-change binary;
the A/B's control arm == committed GT **per-game** in all 8 hinata train cells; scenarios 44/44;
unit 702/702).

**The A/B** (one pooled 18-job / 5950-game batch, `logs/mana_robust/spasm/`): every hinata train
cell moves SLOWER, at every depth — smoke d0 6.9690→7.3180, d3 5.6733→6.0600, d5 5.8533→6.1867;
regression d0 7.0480→7.4320, d3 s2002 5.6800→6.0300, d3 s3003 5.6800→6.1100, d5 s2002
5.6800→6.0400, d5 s3003 5.6900→6.0600 (Δ +0.33..+0.43/cell). Per-game: 890 slower / 18 faster /
**80 newly-unwon** of 2725. Dragonstorm is digest-identical even flag-ON (Irencrag = the fixed-
burst branch, untouched); burn likewise. Per §4's pre-registered expectation this looked like the fix
WORKING — but **§8's full-scale line analysis overturns that reading for most games**: ~97.6%
of the slower lines are payable under honest rules (gi33's 12-pip line is exactly fundable by
tap-out → Spasm-refresh → retap), so the bulk of this slowdown is an engine sequencing gap,
not recovered laundering. Read §8 before acting on these numbers.

**The §5 success criterion is met at 100%**: re-running all 149 formerly-UNPRICED hinata ledger
rows on today's engine, the float arm still reads 139 UNPRICED / 10 LEGAL, while the literal arm
reads **149/149 LEGAL** (zero LAUNDERED in either arm; `logs/mana_robust/spasm/collapse/`). The
"short on raw count" bucket was the Spasm float wearing that label, exactly as §3 sampled, and
with the untap logged the ledger prices every one of those turns exactly.

**Known first-cut artifact** (accepted, refinement deferred): the optimistic enum credit lets
~7% of flag-on Spasm turns end with NO same-turn follow-up cast (8/121 at d3, 9/130 at d0
greedy; flag-off ~1%) — a wasted {U}{U}. The refinement, if wanted later, is §4's payment-aware
projection at the consider/credit sites. Also note `crackle-reality-spasm-overgeneration.md`'s
enum/apply edge is MOOT under the literal model (there is no float to over-generate).

## 8. WHY the slower cases are slower (2026-09-01, full-scale analysis) — §7's doctrine framing was WRONG for most games

USER challenge that triggered this ("most of the time you only need 2 blue + some red on board to
manage that if you have 1-2 Reality Spasm... a big loss is unexpected here") — and the USER was
right. Every one of the 890 slower games (80 newly-unwon included) was analysed with a
constructive **line-payability checker** (`logs/mana_robust/spasm/line_check.py` + results
JSON): take the off-arm's winning-turn casts, the real board's sources, and ask whether ANY
tap/untap schedule pays that line under honest rules. The key structural fact making this exact:
the engine always sizes Spasm's X = TOTAL source count, so X >= #tapped and **every Spasm is a
full board refresh** — with k Spasms, each source is legally usable k+1 times (mana abilities
tap at will within the phase; floats are turn-scoped), so payability reduces to a colour
matching over (k+1) x board + Irencrag, minus filter feeds.

**Result: 869 of 890 slower lines (97.6%) are PAYABLE under honest rules** — including 76 of
the 80 lost wins. Only 21 lines are genuinely unpayable (mostly 30–43-pip multi-Spasm turns
that really were float fiction). Worked example (gi33, smoke d3): board nets 6 mana, line =
Spasm {U}{U} + Crackle {8}{R}{R} = 12 pips; the honest schedule is *tap all 6 into the float →
Spasm off the float (refunds all 6) → retap 6 → Crackle 10*. Exactly 12. The off-arm's T3 kill
was rules-achievable; the float model reached the right answer by unsound accounting.

**So the +0.35–0.43/cell is NOT mostly honesty — it is an engine capability gap.** 232 searched
movers are payable yet unrecovered at UNBOUNDED budget (283 probes, `probe_results.json`):
structural, not budget. Two mechanisms, one probed causally:

1. **Cast order** (secondary, and REFUTED as the lever): both order tables put the untap ritual
   before every payoff (generic 15 < 20; hinata full order 7) — a float-era decision ("consider
   it as soon as possible", sound when the float persists) that under the literal model fires
   the untap on an untapped board: 396 of 488 lit-arm Spasm turns cast Spasm before any big
   spell. BUT the diagnostic reorder `MTG_SPASM_ORDER_LATE` (Opus/Soulfire 20 → Spasm 21 →
   Crackle 22; default OFF, probe-only) measured WORSE than plain literal (951 slower than
   control, 186 slower than lit, 131 unwon) — a static rank cannot express the needed
   interleaving and disturbs plan selection. Do not ship it; kept only as the recorded negative.
2. **No tap-ahead** (the load-bearing gap): the modal kill line (Spasm + one payoff, gi33-class)
   is payable ONLY by speculatively tapping the whole board into the float before Spasm — the
   payment machinery has no such move (it taps exactly to cover costs). **The phase-3 shippable
   shape is therefore: casting an untap-X ritual first taps EVERY untapped mana source into the
   float** — strictly mana-optimal whenever X covers the board (the untap refunds every one),
   rules-legal, and exactly the schedule the checker proves sufficient for the 869 payable
   lines. The enum credit then stops being "optimistic" and becomes ~exact.

Corrected framing: the float model's *accounting* was unsound (the ledger rightly refuses to
price it), but its *outcomes* were ~97% rules-achievable. Adopting the literal model as-is
would trade fictional accounting for a REAL play regression — **do not adopt without the
phase-3 tap-ahead**. The honest GT cost of a correct implementation is bounded by the 21
unpayable lines (~2% of movers), not the measured +0.4.

Also found during this analysis: `prepay_recheck._units_for` modelled a ramp filter (Izzet
Signet) as NET ZERO while the card and the engine are net +1 — every Signet turn was
under-credited one unit (fixed in the same commit; affects future verdicts only, and only in
the fewer-accusations direction; the §7 collapse stands a fortiori).

**The adoption bill, for the USER**:
1. Flip the default (EnvOn(...,true) + hatch) and rebaseline all tiers — hinata GT gets
   honestly worse by ~+0.35–0.43/cell including unwon-at-T8 games (80 in train alone). Per the
   prepay doctrine these are NOT regressions to recover.
2. Hinata's sidecars were generated under FLOAT-model play and embed it: `Hinata2.value.json`
   (ACTIVE by presence) and the exhaustive keep table both graded states in a world with free
   Spasm mana. Honest adoption should regenerate the value leaf (`valueleaf.sh run`) and at
   least re-ask the keep-table question (mulligan-profile.md's commit-bound rule: a play-logic
   fix invalidates prior sidecars).
3. After adoption, re-run the unpriced split to retire the ledger's hinata section for good.
