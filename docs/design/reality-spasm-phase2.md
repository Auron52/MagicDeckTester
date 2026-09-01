# Reality Spasm phase 2 — the untap model, what it still launders, and the rework plan

**Status:** ADOPTED (2026-09-01, commit 4f449fbd) — `MTG_SPASM_UNTAP_LITERAL` is **default ON**
(`=0` restores the float model byte-exactly) and all three GT tiers are rebaselined (7994ce8f).
The overnight (held-out) seeds confirmed the §9 pattern: 12 hinata cells moved net **−0.064
turns**, every cell equal-or-better except two at +0.0025; the 15 searched-slower games
classified as 4 churn + 11 isolated one-turn slowdowns (the colour-commit tax). Remaining
follow-up: regenerate the hinata value leaf on the adopted commit (§9). History: designed 2026-08-30 (§1–§6); first
cut built + measured (§7, commit 53fa08eb); §7's "honest slowdown" reading overturned by the
§8 line-payability analysis; the §8-prescribed tap-ahead built and measured in §9. This is the
doc `unpriced-prepay-rows.md` links as "cause 2's blocker".

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

## 9. TAP-AHEAD BUILT + MEASURED (2026-09-01) — the literal model now BEATS the float model

The §8-prescribed fix, folded into the same `MTG_SPASM_UNTAP_LITERAL` lever (it is the missing
half of the literal model, not a separate switch):

- **`RitualTapAheadIntoFloat`** (SpellEffects.h): immediately before an untap ritual's payment,
  tap every untapped, side-effect-free mana source into the turn-scoped float. Resolution then
  untaps min(X, #tapped) = everything (X is always sized to the full source count), so the
  float is pure profit — the engine version of a human's "tap out, Spasm refunds the board,
  tap again". Capped at X total tapped sources; colour per source via `AddRefloatContribution`
  (ONE rule shared with the float credit, extracted so they cannot drift); side-effectful taps
  (Deathrite exile, pain, storage, domain, creature-only, depletion) skipped — byte-inert
  today, safe-by-construction later. The USER's land-drop caveat is satisfied structurally:
  both the plan apply and the executor play the drop BEFORE the cast loop, so the new land
  (even an enters-tapped Karoo — it arrives tapped and the untap harvests it) is in the
  refresh; drop-vs-hold on bad colours stays a searched decision as before.
- **Three lockstep payment hooks**: the rollout/plan apply, the executor's
  `CastSpellFromHand` (which re-derives `available` — it excludes floating, so the stale
  snapshot would double-count), and `SubsetPayableSequential`. Resolution untap unchanged.
- This makes the enumeration credit **exact**, closing §7's "optimistic bound" caveat:
  realised mana after a Spasm = float(board) + refreshed board = pool + `HinataRitualNetBonus`,
  the same arithmetic the planner uses. The §8 stranded-Spasm artifact goes with it.

**Measurement** (same 18-job/5950-game pooled battery, `logs/mana_robust/spasm/ta_*`):

| cell | float (ctl) | literal, no tap-ahead (§7) | literal + tap-ahead |
|---|---|---|---|
| smoke d0 | 6.9690 | 7.3180 | **6.9590** |
| smoke d3 | 5.6733 | 6.0600 | **5.6733** |
| smoke d5 | 5.8533 | 6.1867 | **5.8400** |
| regression d0 | 7.0480 | 7.4320 | **7.0430** |
| regression d3 s2002 | 5.6800 | 6.0300 | **5.6700** |
| regression d3 s3003 | 5.6800 | 6.1100 | **5.6500** |
| regression d5 s2002 | 5.6800 | 6.0400 | **5.6600** |
| regression d5 s3003 | 5.6900 | 6.0600 | **5.6600** |

(The table is the final COLOUR-COMMITTED build; the first tap-ahead cut floated a
partial-choice source — a Signet's U-or-R — as WILD, and the ledger immediately caught a
tap-ahead Signet paying an off-colour pip: §2 defect (b) re-entering through the float. The
fix: a partial-choice source (2–4 colours) COMMITS to a colour at tap-ahead time — need-aware,
by the hand's remaining pip demand, though on this deck that is byte-identical to first-listed
— while full-rainbow sources keep wild, which is exact. The commit costs a handful of d0
greedy games whose wild-float wins were partly the laundering channel itself.)

Equal-or-better in EVERY cell (7 better, 1 equal). Per-game vs the float model:
**8 slower / 28 faster / 1 newly-unwon / 2 newly-WON** (was 890 / 18 / 80 / 0 without the
tap-ahead). The §8 hand-proved case gi33 recovers its T3 at CASE budget, as do gi86/gi8/gi34/
gi90/gi104 — all previously unrecoverable at unbounded. The residual slower games adjudicate:
gi23 = one of §8's 21 honestly-UNPAYABLE lines (and tap-ahead still improved it 7→6); the d3/d5
pair gi58 + gi120 = coverable (both win the control turn at unbounded budget); the rest are d0
greedy churn/commit casualties inside cells that still net faster-than-control averages. ZERO
unrecoverable searched regressions. Gates: flag-off byte-identical (+ =0 hatch), dragonstorm
digest-identical flag-ON, control == committed GT per-game everywhere, unit 702/702,
scenarios 44/44.

**The ledger collapse under the final build: 148/149 LEGAL** (the tap-ahead's taps land in the
tapped-delta and the untap event credits the refresh, so Spasm turns price exactly).

**The 1 non-LEGAL row is a separate defect found by the collapse re-run (NOT this feature's):** gi164
(hinata_overnight_d3_s4004) prices LAUNDERED under the flag — but its T3 casts NO Spasm and no
tap-ahead fires; the [bp-pay] trace shows `BatchPrepayMainCasts` pre-floated the turn, and the
line's mana only balances if the prepay fed Cascade Bluffs with an off-colour feeder (only Sol
Ring's colourless remained once Mountain's R went to Hinata). A pre-existing PREPAY-path filter-
feed leniency, surfaced because the flag-on search picks a line that exercises it; flag-off play
happens to dodge it today. Belongs to the prepay-recheck workstream: audit the prepay joint
solve's filter feed against tap_source's strict colour rule.

Why the honest model WINS rather than ties: the float was generous in total mana but wrong in
structure — its wild/colour approximations and enum-vs-realised mismatches cost real casts,
while the literal chain realises true colours through the normal payment machinery and the
now-exact credit stops the search being misled. Honesty turned out to be free, and slightly
profitable.

**Adoption recommendation (USER call, but now unambiguous)**: flip `MTG_SPASM_UNTAP_LITERAL`
default-ON. GT movement is small and favourable (the §2 bill's "hinata gets honestly worse"
never materialises — the tap-ahead recovers it). The sidecar-regeneration question (§7 bill
item 2) softens accordingly: play changed far less than the §7 measurement implied, but the
value leaf / keep table were still fitted to float-model games; regenerating the value leaf on
the adopted commit remains the clean-room move. After adoption, re-run the unpriced split on
the shipped default to close the ledger's hinata section.
