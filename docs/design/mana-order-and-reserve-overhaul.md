# Mana order + reserve overhaul — the comprehensive fix (USER, 2026-08-25)

**Status: BUILT (2026-08-25), measurement pending.** All layers implemented, every lever DEFAULT
OFF, clean-env smoke 42/42 byte-identical after the build. The as-built lever map:

| lever | layer | what it enables |
|---|---|---|
| `MTG_TREASURE_PAY_SOURCE` | 1 | §2a Treasure as ranked payment source (pre-existing) + the new rank-26 tier (`ManaSourceRankBase`) |
| `MTG_DORK_TAP_LAST` | 1 | creature band (pre-existing floor shape) |
| `MTG_ONESHOT_RESERVE` | 2 | §2b one-shot hold: prepay ladder class (creatures > one-shots > depletion) AND the per-payment `OneShotHoldMask` reserve-first attempt |
| `MTG_M2_RELEASE` | 3 | post-combat main releases both creature hold classes |
| `MTG_PUMP_TARGET_HOLD` | 3 | provider-narrowed rung (`ReserveCreatureHold`), pump-target-last partial release, and the backtracker's keep-last partition (`g_tap_keep_last_card`) |
| `MTG_SCALER_PLAN_BIAS` | 3 | plan-conditional scaler slot in the band (food F+2 / attack F / else F+1; domain F+2); needs `MTG_DORK_TAP_LAST` |

New machinery: `PlanTraits` + `PlanTraitsScope` (`ai/PlanContext.h`), built once per apply by
`TurnSolver::ComputePlanTraits` (both apply sites, lockstep), null = byte-identical;
`DecisionProvider::ReserveCreatureHold` / `SpendOneShotsFreely` hooks with the generic defaults and
the `MirrorwingProvider` copy-magnet overrides; `TapKeepLastScope` mirrors the pump target into
core for the backtracker (the `g_flow_src_mask` pattern).

Supersedes the piecemeal state spread across
`mana-creature-tap-order.md`, `lump-mana-sources-as-payment-sources.md` (§2a/§2b/§9) and
`mana-source-reservation.md` — those stay as the measurement record; this is the build plan.

USER directive (2026-08-25): *"build the comprehensive fix. That would be an order for most
sources, default reserve order and finally a means to override the default reserve order based on
the plan, phase and board status. If possible, I would like the latter to be done in as generic a
way as we can in the GenericProvider and be inherited, but have a way to override parts in the
derived classes."*

No run is waiting on this, so the whole bundle is decided TOGETHER and adopted ONCE (one GT
rebaseline). Sequencing fact that motivates deciding it as a unit: the shipping Mirrorwing keep
table was generated under fan-enumerated Treasures; the moment any of this adopts, pooling more
seeds into that sidecar closes (play digest changes). Adopt once, not piecemeal.

## The three layers

```
1. TAP ORDER      ManaSourceRank ladder      "once sources must pay, which pays first"
2. RESERVE        prepay hold ladder          "which sources does the whole turn try NOT to tap"
3. OVERRIDE       plan/phase/board traits     "when does the default reserve/release flip"
```

Layer 1 is per-pip and static-ish; layer 2 is whole-turn (`BatchPrepayMainCasts`); layer 3 is the
new part — computed once per prepay from `acts` + `state.phase` + board, consulted by both.

## Layer 1 — the source order (finalize the ladder)

Adopt, as one coherent ladder (every item already measured; citations in the source docs):

| rank | tier | status |
|---|---|---|
| 5 | {C}-only source | SHIPPED (`eaccc120`) |
| 10/20/30/50 | mono/dual/tri/rainbow lands | shipped |
| 25 | filter / ramp-filter | shipped |
| **26** | **pay-sac one-shot (Treasure, §2a)** | swept 25/26: −0.0050 train, −0.0044 held-out t=−6.77; replace `MTG_PAYSAC_RANK` scaffolding with a real tier |
| +1 | drip land nudge (Grove) | shipped; live-drip = max-scarce exception preserved |
| **51+** | **mana creatures (band above ALL lands)** | floor 51 vs 64 — final in-bundle sweep decides (§5 of mana-creature doc); subtype scalers stay WITH flat dorks (§5b), domain scalers held one slot later (§5b's winning arm) |
| 59 | coloured-manland cap | shipped |
| 60–63 | reserve tiers ({C}-manland, scaled dork 61, storage 62, untap-burst 63) | shipped, untouched |

Prerequisite folded in: **§2a default ON** (`MTG_TREASURE_PAY_SOURCE`, hatch stays) — 1.50x on
Mirrorwing, quality-neutral after `6d3c61df`. Its two remaining gates (multi-deck regression pass,
GT) are subsumed by this bundle's gates.

Standing law from six refuted variants: **no creature ever ranks ahead of any land.** Three
separate projects broke `references/Anti-Lifegain/claude_s5_gi4.json` by licensing that; the band
keeps creatures strictly after lands and only the RESERVE layer decides releases.

Lockstep obligations: the scarcity selection is duplicated in `AIEngine::TapForCost` and
`TurnSolver::TapForCostDirect` (change both); the backtracker keeps its creature-last stable
partition (rank-sort of its candidates measured worse — do not "unify" that).

## Layer 2 — the default reserve ladder

`BatchPrepayMainCasts` hold classes become three, in priority order (held hardest first):

```
MANA_CREATURES  >  ONE_SHOTS (pay-sac Treasures)  >  DEPLETION lands
```

* Creatures first: a body is an attacker / trick target / sac fodder (shipped doctrine).
* One-shots second — **§2b "waste is the trigger", NEW**: a spent Treasure is gone forever, a
  depletion counter is one of N and mono-colour, a land untaps free. Holding the Treasure whenever
  the turn pays without it is exactly the rule whose absence blocked creature-taps-last (the
  Mirrorwing s26 gi25 reference: rank made the payment crack the Treasure the T4 kill needed).
* Depletion third: counter is mana either way (shipped rationale).

Rungs stay a short ladder (drop the lowest-priority class first), same
byte-identical-when-class-absent construction as today. The partial creature release
(`MTG_DORK_HOLD_PARTIAL`, weakest `EffectivePower` first) is measured in-bundle — with layer 3 it
gains a better release order than raw power (see below). Storage lands keep their existing
full-hold + rank-62 treatment. Mana rocks stay unreserved. Lotus-class (amount ≥ 2) stays at the
action level (`MTG_SAC_COLOR_FOLD`, adopted) — out of scope here.

## Layer 3 — the override: plan, phase, board

### PlanTraits — computed ONCE per prepay, cheap by construction

A small POD derived from `acts` + `state` at `BatchPrepayMainCasts` entry (and installed over the
whole apply via the extended `PlanContextScope`, so per-pip code reads a thread-local pointer, no
recompute). Only `a.def` / `LookupCached` — no string-keyed lookups (the `MTG_PREPAY_FASTDECLINE`
lesson: two passes with string lookups was a measured cost at 4.4M calls/rollout).

| trait | source | consumers |
|---|---|---|
| `main2` | `state.phase` | release bodies (goldfish: combat is over; revisit for 1v1 blocking) |
| `pump_target` | plan has a targeted own-creature pump → its PROJECTED target | narrow the creature hold |
| `copy_magnet_live` | board has Zada/Mirrorwing-class copier | archetype override input |
| `bodies_are_multipliers` | targeted pump/copy trick in plan (given magnet) | one-shot spend bias (gi81) |
| `casts_scaler_food[subtype]` | plan casts creatures matching a board scaler's subtype | scaler tap bias |
| `attack_matters` | m1 + an eligible attacker with power/exalted relevance | scaler tap bias, body release |

### The generic defaults (GenericProvider), each a small overridable virtual

1. **`ReserveCreatures(state, traits)`** — which creatures the prepay tries to hold, and the
   RELEASE ORDER when they compete.
   * main 2 → hold none (release the bodies; combat happened). Caveat: don't double-count a
     vigilant scaler that attacked (Faeburrow doctrine).
   * targeted pump in plan → **hold only the pump target** (USER 2026-08-25: *"with targeted
     pumps I would have the default only reserve the pumped creature"*); the rest release first.
     USER notes the current mechanism "works, but uses a hacky approach": `AttackerReserveEnabled`
     holds the greatest-power mana-source attacker on EVERY turn, pump or not, betting the apply's
     auto-target lands there (a `CastFromHand` Action declares no creature target — targets
     auto-resolve at apply). The non-hacky shape without breaking the `--choices` protocol: ONE
     shared projected-target function (the same picker the apply's auto-resolve uses), consulted
     pre-payment by the reserve and gated on the plan actually CONTAINING a targeted pump — so the
     hold fires exactly when a pump is in the plan and holds exactly the creature the pump will
     target. Moving target declaration into the Action (enumeration-time) stays rejected — it
     reorders `--choices` and breaks pump references (mana-source-reservation.md).
   * otherwise → hold all (shipped leave-out-if-you-can), release weakest-power first.
2. **`OneShotSpendBias(state, traits)`** — hold vs spend the pay-sac one-shots.
   * default: HOLD when the turn pays without them (§2b); on a `bodies_are_multipliers` turn the
     bias flips — spend the one-shot to keep bodies untapped (the gi81 rule, both halves).
3. **`ScalerTapBias(state, traits)`** — USER 2026-08-24: attack turn → tap the scaler EARLY (one
   body pays all, other dorks stay up to swing); `casts_scaler_food` turn → tap non-scalers first
   so the scaler's burst lands AFTER this turn's creatures. Resolves the §5b subtype-vs-domain
   tension plan-conditionally instead of statically.

### Archetype overrides (the point of the shape)

* **Mirrorwing/Zada (copy magnet)**: `ReserveCreatures` override — when pumping, hold the ENTIRE
  board of creatures (USER 2026-08-25: every creature is a copy target), and per §9's recorded
  inversion spend Treasures before creatures there.
* **Anti-Lifegain is explicitly NOT an override** (USER 2026-08-25: *"I would like it not to need
  an override for invigorate. We should reserve naturally there."*) — Invigorate is a targeted
  pump, so the GENERIC pump-target rule must hold its target with no archetype code; treat this
  deck as the validation case that the generic default is right. The live-drip exception (a live
  Grove ranks max-scarce so the drip fires — the mechanism that broke `claude_s5_gi4` three
  times) is already shipped in RANK space via the `tap_opponent_lifegain` param + the existing
  `OpponentLifegainUseful` hook, and is orthogonal to reservation; it must simply survive any
  narrowed hold.
* Everything else inherits the generic defaults untouched.

Template-method, not a monolith override: derived classes override the narrow virtuals, never the
ladder assembly, so a deck provider states only its doctrine delta.

## Hard constraints carried forward

* **No plan fan-out on tap choices.** `UnprunedGate::TapReserve` measured 3.08x for worse play;
  USER: "we can't afford to branch on mana decisions." The plan CONDITIONS the heuristic; it never
  multiplies plans. The gate stays available as an instrument.
* **No lossy truncation** (heuristic skill Rule 0b): everything here is hold-with-fallback or a
  candidate permutation; the complete backtracker remains the fallback; `MTG_UNPRUNED` semantics
  unchanged.
* Every flag via `EnvOn` (coding-conventions); shared executor/rollout readers in `EngineFlags.h`;
  adopted pieces keep an off-hatch; scaffolding (`MTG_PAYSAC_RANK`, `MTG_DORK_RANK_*`) deleted at
  adoption with the outcome recorded.
* Executor/rollout lockstep through the shared functions (`BatchPrepayMainCasts`,
  `PlanReserveSources`, one traits builder) — never two copies.
* Dig/flood turns: prepay declines (drawsafe) — traits/holds must degrade to today's behaviour
  there. `CurrentPlanContext() == null` ⇒ behave exactly as before, everywhere.

## Adoption bar — STRICT improvement (USER, 2026-08-25)

*"We should aim for strict improvement given the design we are making."* This design is entirely
hold-with-fallback rungs and coverage-preserving reorderings — there is no structural reason for it
to lose any game — so the adoption gate is per-GAME, not per-aggregate:

* Every searched-depth SLOWER game on every seed set must classify as budget CHURN
  (`classify_turn_later.sh`: recovers to the old win turn at 4x/16x) or be ROOT-CAUSED and fixed;
  a PERSISTS with same draws is a defect in a lever, not a cost to average away.
* **Churn itself must be attributable (USER, 2026-08-25):** the reserve/rank levers do not touch
  the plan LIST — same plans, same budget-per-plan — so they cannot legitimately churn. The one
  plan-space changer is §2a: it deletes the sac odometer groups (anti-dilution) and effectively
  ADDS lines the capped fan never enumerated (EnumGroupCap/fungible-cap bounded the crack-combos;
  payment-native Treasures make them all reachable). Churn is acceptable only in games whose plan
  space §2a changed (deck has one-shots, plan counts moved); churn anywhere else is a defect.
* d0 (no budget, the honest heuristic read) should have no unexplained slower games at all.
* A lever whose only effect on some deck is negative on all seed sets is dropped or narrowed —
  net-positive aggregate does NOT carry a per-deck regression.

## Measurement plan (heuristic-optimization skill)

1. Each layer behind its own lever; smoke sweep (s1001) per lever and for the bundle.
2. Bundle A/B on regression (s2002/3003), then HELD-OUT overnight seeds before any adoption talk.
3. **References strict on all 224 — DONE (2026-08-25), gate GREEN under the full bundle.**
   `claude_s26_gi25` reproduces its exact T5 kill (the creature-taps-last blocker is CLEARED —
   §2b + §2a together preserve the Treasure the kill needs) and `claude_s5_gi4` stays clean.
   Getting there required a REPLAY-PROTOCOL repair, user-approved conditional on outcomes
   (USER: *"okay giving permission as long as we can reproduce the same win turn"*): §2a deletes
   Treasure `SacForMana` actions, so fan-era recordings carried plans and re-prompt frames that
   cannot occur verbatim — `viewer_protocol_check.py` gained (a) a §2a match tier (recorded plan
   MODULO its Treasure-sac actions), (b) a stale-PASS-frame skip (collapsed re-prompts; a stale
   frame with a real cast is never skipped), (c) a loose-replay degrade (plan miss after upstream
   defaults ⇒ default-pass and let the final-outcome drift test decide, matching baseline's
   looseness), and (d) BOARD-aware gap classification (an "enum-gap" whose gap-frame board —
   names + tap state — differs from the recording is the shuffle-dead class, not an enumeration
   gap; hand equality alone cannot assert "same state" when the whole change under test is which
   sources stay untapped). All compat paths are env-gated on `MTG_TREASURE_PAY_SOURCE`
   ((d) is unconditional but baseline-inert); the baseline sweep is unchanged (0/0/0 over 224).
   Two refs report board-diverged (FiveColour s9_gi8, StompySurprise s11_gi10 — upstream taps
   legitimately differ under the bundle): only re-playing can restore them; that is the USER's
   call, listed for the adoption report. Mirrorwing s3_gi2 (an old, loose ref: 7 defaulted
   decisions at baseline) replays to its exact T7 via (c); its transient enum-gap was proven
   NOT an engine change (baseline enumerates identically at the same state).
4. `classify_turn_later.sh` on every searched-depth slower game (churn vs persists).
5. In-bundle decisions the sweep settles: creature floor 51 vs 64; domain-scaler hold slot;
   partial-hold on/off; Treasure rank 25 vs 26 re-check under §2b.
6. Report per-deck deltas + slower games + noise caveat to USER; adopt on approval; one GT
   rebaseline (smoke/regression/overnight); then the Mirrorwing pooling/regeneration decision.

## Strict-bar ledger (2026-08-25, post one-shot-hold fix) — WHERE THE WORK IS

The one-shot hold shipped a real defect and was FIXED the same day: the per-payment hold fired
with NULL traits (search-interior payments) and on multi-cast plans — re-creating the retired
MTG_RESERVE stranding; traced on MW gi75 (T+O, unbounded d5: deep rollouts under the bare hold
flipped a near-tie T2 pick). Now: live traits + single-mana-cast plans only. That recovered
gi75/gi86/gi141 outright. Post-fix suite state: smoke searched 4 slower / 49 faster; regression
searched 14 slower / 170 faster; d0 25 / 227 (smoke), 39 / 256 (regression).

Every remaining slower game is ATTRIBUTED (per-lever single-game matrices, then unbounded-d5
persistence). The persistent-at-unbounded set — the strict bar's real debt:

| class | games | mechanism | next action |
|---|---|---|---|
| §2a core | ~~mw68, mw81 remain~~ **ALL RECOVERED** — see "§2a debt RESOLVED at the root" below (BuildNonCreaturePool was the blocker) | Value-leaf theory REFUTED; per-lever ablation pinned the class on §2a alone; root cause found and fixed 2026-08-25. | done — residual near-ties mw43/mw148 tracked in the resolution section. |
| band (D) | cg30 (d3+d5), fc96, st139 persist; fc34/35, st252/292, +12 d0 (FC 6, MW 5, ST 1) recover or are policy shifts | tap order is NOT a search branch, so band losses cannot be budget-recovered; the rollout POLICY change re-scores plans (cg30: the Orchard-tap dynamics of Creature Giving — Orchard taps ARE damage with Suture Priest out, so band's lands-first is entangled with the win condition). | per-deck traces; likely a Creature-Giving-style drip exception (the Grove precedent: rank a live "tapping is damage" source appropriately) rather than weakening the band. |
| m2-release (M) | fc98 (persists unbounded) | untraced | trace next. |
| horizon | mw148 (d3 only, d5-unbounded recovers), mw173, fc34/35, st252 | §2a plan-space churn (acceptable class per USER bar) or depth-horizon | verify each recovered game's class tag once the above fixes land, then re-sweep. |

### §2a debt, re-measured 2026-08-25 (post clause-(c) fix, binary at DecisionProviders.cpp 03:41)

Three results, in the order the ledger asked for them.

**1. The value leaf is NOT the cause — hypothesis refuted.** `logs/overhaul_sweep/leaftest/`: with the
sidecar disabled, mw68 is base T4 / lever T5 and mw81 is base T4 / lever T5. Disabling the leaf
recovers neither. The "trained under FAN-world play, mispricing the horizon" theory is dead; do not
re-derive it.

**2. The clause-(c) fix did not recover mw68 or mw81** — even though its comment cites the mw68
trace as its motivation. Re-run at unbounded d5 under the current tree: mw68 BASE 4 / FULL 5,
mw81 BASE 4 / FULL 5. mw22 and mw176 DO now recover (4/4), so the fix is not inert — it just does
not reach these two.

**3. Per-lever ablation pins both games on §2a ALONE.** Unbounded d5, drop-one arms:

| game | BASE | FULL | T_only | noT | noO | noM | noP | noD | noS |
|---|---|---|---|---|---|---|---|---|---|
| mw68 | 4 | 5 | **5** | **4** | 5 | 5 | 5 | 5 | 5 |
| mw81 | 4 | 5 | **5** | **4** | 5 | 5 | 5 | 5 | 5 |

`T_only` reproduces the loss and `noT` removes it, for both. **No reserve or tap-order layer
contributes.** Layers 2 and 3 are clean on this debt; the whole remaining §2a debt is §2a's.

The traces then split them into two DIFFERENT shapes — they are not one class:

**mw68 (s3003 gi68) — a LOST LINE, and this looks like a real defect.** Identical play through T3
(Needle, Forest + Instigator, Crag + Zada, attack to 18). On T4 with the magnet live:

```
BASE    T4  Forest, Gold Rush, Fists of Flame   -> attack: 18 -> -8   WIN T4
§2a     T4  Forest, Gold Rush                   -> attack: 18 ->  1   (win T5)
```

Under §2a the engine **casts Gold Rush, mints the Treasures, and then does not cast Fists of Flame**
— which is still in hand (card 5) at end of T4 main, with the Treasures still on board (battlefield
9 permanents vs BASE's 8, i.e. BASE spent them). So this is not an affordability shortfall and not a
valuation shift: the mana and the card were both there. The natural suspect is that the payment view
is computed from the board as it stands at plan-enumeration time, so a Treasure minted EARLIER IN THE
SAME PLAN is not visible as a payment source for a later cast in that plan — precisely the line the
fan world could express as an explicit `SacForMana` action and the payment-native world may not.
**That hypothesis was TESTED AND REFUTED (2026-08-25) — do not re-derive it.** The credit block in
`FirstUnpayablePos` does bank only the BASE `creates_treasures` (1) and its own comment defends that
as "the safe direction", so the obvious theory was that Fists's `{1}{R}` is scored against 1 wild and
the line is called unpayable. A temporary `MTG_PAYSAC_FAN_CREDIT` hatch was built that models the
real fan width (recipients = every own creature-or-animated permanent, matching
`ResolveTrickCopies`' recipient list) and credits `creates_treasures * recipients` when a magnet is
live. **mw68 stayed T5 and mw81 stayed T5 under it.** The under-credit is therefore NOT the blocker,
and the diagnostic was removed again (the tree is back to byte-identical; `mirrorwing_smoke_d3_s1001`
reproduces `4.7200/bf00585158c29bdc` with levers off). Note the risk that made it diagnostic-only
still stands if anyone revisits it: `trick_up_to_one` means an untargeted Gold Rush is legal and
mints exactly 1, the Action carries no target, so modelling the fan OVER-credits on that line.

**So the mw68 mechanism is still OPEN.** What is now known: the card is in hand, the Treasures are on
board and unspent, a magnet is live so `PaySacSpendableNow`'s fresh-hold permits them, and the
payment-projection credit is not what drops the cast. The remaining suspects are upstream of payment
— whether the plan enumerator ever EMITS the {Gold Rush, Fists} pair under §2a (the sac-odometer
groups §2a deletes were how the fan world expressed that line), or whether it is emitted and loses on
score. Instrument the enumeration next, not the payment.

**mw81 (s1001 gi81) — a genuine valuation shift, not a lost line.** Here the lines diverge at T3:
BASE plays Zada on T3 and wins T4 (Hierarch + Gold Rush + Fists + Gold Rush, exact lethal at 0).
§2a instead develops on T3 (Instigator, Hierarch, Oracle's Restoration), plays Zada T4, and wins T5
with enormous overkill (-83). That is the "development picked over Zada-first" class the ledger
already named. It is a preference change, so it is a judgement call against the strict bar rather
than a defect to fix — and it should be decided AFTER mw68, since a §2a coverage fix may re-price
these lines anyway.

Aggregates for perspective (post-fix): every moved case's avg improves or is digest-only EXCEPT
creature_giving d3/d5 s3003 (+0.003/+0.004 — cg30 is that case's one game). The two biggest
gains remain stompy (−0.10..−0.14 every case) and mirrorwing (−0.03..−0.14).

Artifacts: `logs/overhaul_sweep/` — per-arm smoke logs, `arm_BUNDLE_fix_{smoke,regression}.txt`,
`unbounded_battery.txt`, `traces/` (gi75/gi81/mw68/mw176/mw22/mw81/cg30 base-vs-lever game logs).
Attribution scripts: `/tmp/attrib*.sh` (ephemeral; the matrices are recorded above and in the
battery file). NOTHING IS COMMITTED YET — engine layers, VPC compat, and this doc are all local.

### §2a debt RESOLVED at the root, 2026-08-25 (later the same day; second agent session)

The "instrument the enumeration next" pointer above closed the case. The blocker was never the
enumeration of the {Gold Rush, Fists} pair — it was **`BuildNonCreaturePool` (TurnSolver.cpp
~414), which the §2a swap never converted**. Every §2a pool/colour scan except this one had
learned to count a spendable Treasure; the noncreature affordability test therefore rejected any
noncreature cast whose payment needed the fan Treasures. Instrumented proof (`MTG_BP5_TRACE`, a
kept diagnostic in the deferred site-5 re-solve): at mw68's committed T4 post-Gold-Rush state
(`pool=4 tre=3 magnet=1`), the greedy's candidate list contains Fists at top eval (e900), a bare
`TapForCostShared` on the same state pays it — and the continuation still solved EMPTY, because
pool_nc saw only the Crag (1 < 2). The opponent survived that attack at exactly 1 life.

**Fixes landed (all inert with the lever off — `IsPaySacSource` is then always false):**
1. `BuildNonCreaturePool` — add the pay-sac term (the root fix).
2. Broken-lockstep twins found by audit and converted the same way: `AvailableManaPoolNoAttackers`
   (its header says "keep the two loops in lockstep" with `AvailableManaPool`; the §2a swap broke
   that), the spare-pool-minus-best-attacker scan, the two colour-criticality scans
   (`BuildColorFeasibility`'s remove-one walk + the per-colour `makers` list).
3. The flow-feasibility oracle's three scans upgraded def-only `IsPaySacSource` → state-aware
   `PaySacSpendableNow` so the oracle never promises a fresh-held Treasure the payer refuses.
4. **§2a FRESH-HOLD** (built earlier the same day; kept): `PaySacSpendableNow` /
   `MTG_PAYSAC_FRESH_HOLD` (default-on under §2a, `=0` for A/B) — a pay-sac source that ENTERED
   THIS TURN pays only when a copy-magnet is live. Implements the USER doctrine ("magnetless Gold
   Rush is never a this-turn mana play") at the payment layer; recovered mw22/mw176 on its own.
   The mint credit in `FirstUnpayablePos` is gated the same way (`MintedTreasureSpendable`).
5. A clause-(c) next-turn-acceleration tightening of `TrickCastSensible` was built, measured, and
   **REVERTED the same day** — it only compensated for the pool defect, and it cost mw74's winning
   T2 bank, which is SPECULATIVE (Mirrorwing drawn NEXT turn, made castable by the banked
   Treasure) and therefore invisible to any hand-aware acceleration test. Legacy
   `gas_mv_sum > pot` restored; with honest pools the search itself separates mw68's bad early GR
   (a body forgone) from mw74's good one. Recorded under Rejected shapes below.

**Per-game outcome at unbounded (base vs bundle):** mw22 4/4, mw68 4/4, mw74 4/4, mw81 4/4,
mw173 4/4, mw176 4/4, mw56 5/5 (churn) — the entire original §2a core recovered. The value-leaf
theory stays refuted.

**Suite position under the full bundle (fix4, `arm_BUNDLE_fix4_*`):**
- smoke: searched **3 slower / 50 faster** (was 4/49): fc96, fc98 (pre-existing non-§2a), mw148.
- regression: searched **11 slower / 178 faster** (was 14/170): cg30 ×2, fc34, fc35, mw43 ×2,
  mw56, st139 ×2, st252, st292.
- reference gate GREEN (0 play-drift / 0 enum-gap over 224; 2 known shuffle-dead awaiting USER
  re-play). Clean-env smoke byte-identity re-verified on the final tree.

**Remaining debt:**
| class | games | state |
|---|---|---|
| §2a near-tie | mw43 (T2 spends a fan trick magnetless that base holds for the T4 fan), mw148 (T1 land-pick flip) | persist unbounded; both are small-margin plan-preference flips under honest treasure pricing — trace-level understanding recorded, no mechanism defect found. Judgement call vs the strict bar. |
| §2a churn | mw56, fc34, fc35, st252, st292 | base=bundle at unbounded; budget churn in the §2a-changed plan space (the USER-accepted class). |
| band | cg30, fc96, st139 | unchanged today; prior ledger entries + next actions stand. |
| m2-release | fc98 | unchanged today; still untraced. |

### Adoption push, 2026-08-25 (cont.): held-out validation + root cause #3 (Lodge band rank)

USER restated the adoption bar mid-session: **"average better, no uncoverable earlier win-turns."**
An uncoverable earlier win-turn = a game where GT wins earlier and the bundle cannot recover it at
any budget (the classifier's PERSISTS class, minus draw-divergence variance and base-budget flukes).

**Held-out overnight (fix4 bundle, seeds 4004–10010, `arm_BUNDLE_fix4_overnight.txt`):** searched
62 slower / 946 faster. Per-deck mean deltas (negative = faster): stompy **−0.112** (every case
−0.096..−0.134), mirrorwing **−0.061**, fivecolour **−0.022**, th −0.002, everything else 0 or
noise (worst any-deck mean +0.0004). "Average better" is met decisively. Classifier
(`classify_fix4_overnight.txt`): 27 churn / 35 PERSISTS — but ≥6 of the persists are BETTER than
GT at 16x budget (budget-shifted improvements, e.g. mw gi88/gi237 8→loss at case budget, 16x=6),
not regressions.

**ROOT CAUSE #3 — the Lodge band-rank inversion (the whole stompy persist block).** All 10 stompy
persists (uniform 4→5) recovered under bundle-minus-D, and D carries stompy's entire −0.11 (noD
deltas −0.002..−0.012), so D had to be fixed, not dropped. Mechanism (st139 EWINS: the
{Priest of Titania, Turntimber Symbiosis} pair vanishes from the candidate set under D): the
live-burst untap land (Wirewood Lodge) ranks 63 — "past the scaled dorks" — but that invariant was
written against the STATIC dork rank 61. The band moves every dork to 64..66, so the Lodge tapped
BEFORE the Priest, its at-fire-time yield read an untapped board, and the burst died. Fix: the
burst rank is band-aware (`kGrowableManaCreatureTapRank + 2` when the band is on; 63 unchanged
off). Result: st139/st4/st351/st532/st607/st688 recover to 4; stompy overnight aggregate preserved
(fix5 deltas −0.096..−0.130 ≈ fix4). st374/st993 remain (a different D shape: a T1
Llanowar-cast-vs-hold near-tie flip; traced, no mechanism defect found).

**Classification refinements this session:**
- fc35: base at unbounded is ALSO 6 — GT's 5 was a shallow-budget fluke base's own deeper search
  abandons. Not a lever defect; every single lever reproduces 6 identically.
- cg30: proven fetch-shuffle variance (the D arm casts Crop Rotation a turn earlier; CR shuffles;
  T3 draws diverge). fc96: same class (a T1 fetch near-tie pick → different shuffle).
- fc98 mechanism: m2-release defers T3 to main-2 and picks Faeburrow (mana) over Jared Carthalion
  in main-1 (board/loyalty); at the d3 horizon the eval prefers the mana. Near-tie, no defect.
- mw148: the lever arm casts a cantrip base doesn't → draw-offset divergence (no shuffle, but the
  sequence shifts).
- mw43 / mwo344-class (§2a): a pure pump trick (Luxurious Libation) spent magnetless one turn
  before the in-hand magnet lands. Open as a near-tie; see rejected shape below for why a gate
  cannot fix it.

**Rejected shape (third strike for gate-level holds):** a magnetless FAN-TRICK hold over all solo
tricks (hold a pure pump payload when a magnet in hand is castable off next-turn pot) was built
and REVERTED within the hour: it did not flip mw43/mwo344 (both cast the magnet through a
same-plan Treasure bank the next-turn-pot test cannot see) and regressed mwo127 (a held trick was
the right tempo play). Pattern now confirmed three times ((c)-tightening, PAYSAC_FAN_CREDIT,
fan-trick hold): this §2a debt class yields to HONEST ACCOUNTING fixes, never to admission gates.

**FINAL fix5 position (all three suites re-run + classified, `classify_fix5_overnight.txt`):**
- smoke: 3 searched slower / 50 faster — fc96 (shuffle variance), fc98 (m2 near-tie), mw148
  (draw-offset near-tie).
- regression: **6 slower / 186 faster**, every one already classified benign (cg30 ×2 variance,
  fc35 base-budget fluke, fc34/st252/st292 churn). mw43 recovered via the lockstep-pool fixes.
- overnight: 55 slower / 939 faster; classifier 26 churn / 29 PERSISTS. Of the 29: 7 are BETTER
  than GT at 16x (mw gi38/gi88/gi109/gi237/gi295, ds gi188, hinata gi304 — budget-shifted
  improvements, not regressions); fivecolour ×5 + ds gi227 are fetch/shuffle-variance-class;
  the honest residue is the MW same-draws block (~10 unique games: gi43, gi127, gi136, gi242,
  gi292, gi326, gi344, gi370, gi377 + d3 twins) and st374/st993.

**Residual uncoverable set (the judgement call for the USER):** the MW §2a near-tie block above
(+ train mw148), D near-ties st374/st993, m2 near-tie fc98. Each is a small-margin
plan-preference flip with the mechanism understood and no accounting defect found; they ride
against held-out per-deck means of stompy −0.110, mirrorwing −0.061, fivecolour −0.022 (TOTAL
−0.0139/case, searched 939 faster vs 55 slower).

Artifacts: `arm_BUNDLE_fix5_{smoke,regression,overnight}.txt` + `fix5_*_wins/` (final validation),
`arm_noD_stompy_overnight.txt`, `arm_BUNDLE_fix5_stompy_overnight.txt`, `classify_fix4_*.txt`,
`traces/` (st139/st374/st4/st351, fc98, cg30, mw43, mwo344, mw74, mw148, fc96).

### References must never need re-play (USER directive 2026-08-25 — VERY HIGH PRIORITY)

USER: *"Preventing the need for a user to replay games while still keeping them useful is a very
high priority."* Full 224-ref audit on the final fix5 tree (`vpc_clean.txt` / `vpc_bundle.txt`):

- **Clean engine: 224/224 healthy** (131 ok, 93 repaired, 0 play-drift, 0 shuffle-dead, 0
  enum-gap). Nothing is broken today; the earlier "two board-diverged refs await re-play" note is
  OBSOLETE.
- **Bundle: 223/224.** StompySurprise s11_gi10 is now `repaired` (the §2a pass-frame-collapse
  compat covers it; win turn 6=6). The ONE remaining case is **FiveColour claude_s9_gi8**:
  at T4 post-main the recorded plan (`cast: Mana Cannons, Faeburrow Elder, Oko`) is no longer
  among the enumerated plans (42→36) because the levers changed an EARLIER payment's tap
  selection — an engine-automatic sub-decision the reference format does not record — so the
  board (tap state) walked away from the recording even with every recorded USER choice honored.

WHY this class exists: a reference pins the user's *choices*; the engine-automatic sub-decisions
(who attacks, which sources pay) are the engine's business, and the overhaul's whole point is
changing the second — which also moves the first, because the attack heuristic reads the
spare-mana pools. So replay-by-enumeration-lookup is fragile against tap-order work by
construction.

**RESOLVED 2026-08-25 — root cause was upstream of the failing frame, and in TWO parts:**

1. **The diverging automatic decision was the ATTACK, not a payment tap.** Under the bundle the
   T3/T4 attack declarations differed from the recording (T4 sent Deathrite Shaman in; the
   recording held it back as a post-combat mana source). References DO record every combat (the
   play-viewer `events` name the attackers), so the replay now pins them: a new turn-keyed
   side-channel `--force-attackers "turn:A|B;..."` (parsed like `--cast-order`; consulted in
   `DeclareAttackerIndices`, player 0 only, nulled by `RevealLogPause`; overrides willingness
   `AttackWith`, never legality `CanAttackFull`), reconstructed automatically by
   `viewer_protocol_check.py::recorded_attackers` from the combat events. Only turns WITH a
   recorded combat event are pinned (the event fires only when damage landed, and the final
   lethal swing often has no later frame to carry one — so "no event" cannot be read as "no
   attack"). This moved the case from shuffle-dead (board diverged) to enum-gap (state identical,
   plan still missing) — exposing part 2.
2. **A REAL `MTG_DORK_TAP_LAST` defect, isolated by per-lever bisection on the now-identical
   board.** The recorded T4 line is a 9-for-9 exact cover (3 duals + Deathrite + Faeburrow's 5);
   under the lever alone both the enumerator AND CheckLine's independent affordability sim called
   it unpayable, because T3's payment had tapped Deathrite — exiling the graveyard fetch land,
   its nonrenewable fuel — where clean paid with lands+Faeburrow and spared it. The band had
   collapsed the accidental protection the colour ladder gave (DRS's any-colour = rainbow 50,
   after domain ~30): DRS landed at F/F+1, BEFORE the domain grower, in both band paths. Fix:
   `kFuelManaCreatureTapRank = 67` — a `gy_land_exile_mana` dork taps after EVERY other creature
   (its tap destroys a future activation; every other band member's tap is recovered at untap).
   USER doctrine, confirmed 2026-08-25: *"We should be prioritizing tapping most other dorks over
   Deathrite because Fetches are a limited resource"* — with the USER's explicit caveat that this
   is NOT generic against a domain grower: *"You might tap those [Faeburrow Elder / Bloom Tender]
   after to get the full value of their tap"*, *"or for the higher attack damage of Faeburrow"*
   (vigilance + per-colour growth: *"Faeburrow elder is pretty easily a 5/5, which can be a
   significant part of our plan to take down the opponent"* — a PRE-combat mana tap forfeits that
   attack, *"unless it is the second main of course"*, where the attack is already banked and its
   full-yield tap is exactly what to spend). And the pre-combat forfeit itself is a VALUED
   comparison, not a rule (USER 2026-08-25): *"if what you drop can attack itself for more...
   tapping it to play Cosmic Spider-Man when we have a smaller Faeburrow Elder is the correct
   play"* — the s9_gi8 recording's own T3 is that trade (tap the ~3/3 Faeburrow, deploy the hasty
   5-power Spider-Man, attack for net +2). The engine prices this at the right layer: WHETHER a
   pre-combat cast is worth an attacker's tap is search-scored per line (the holds are lossless
   ladder rungs that release when infeasible, never vetoes), and WHICH body then taps follows
   weakest-EffectivePower-first release / pump-target-last. So fuel-last is the measured
   greedy DEFAULT; the grower-vs-fuel exceptions are whole-turn calls that belong to the reserve
   ladder (engine play) and the `--tap-pref` pin (reference replay), never to the per-source
   rank. The Lodge burst rank moves kGrowable+2 → +3 to keep out-ranking the whole band
   (behaviour-identical today: nothing else occupies 67/68, no deck holds Lodge + fuel dork).
   This is the same defect family as the Lodge inversion (root cause #3) and the exact hazard the
   band's own DEFAULT-OFF comment recorded ("makes the payment reach for the ONE-SHOT instead").

3. **The fuel rank alone TRADED refs — the recording is the only complete arbiter of taps.** With
   the fuel fix in, the bundle sweep surfaced FiveColour s13_gi12: its recorded T4 pre-main SPENT
   Deathrite and KEPT Bloom Tender (whose multi-yield paid the post-combat Cannons+BoP), i.e. the
   exact opposite pairwise order from s9_gi8's board. No static rank satisfies both; which sources
   pay is whole-turn-context-dependent (in engine play the reserve ladder carries that context;
   the depth-0 human replay has none). But the recording WITNESSES the taps wherever the engine
   re-prompted in the same phase: the tapped-delta between two same-(turn, phase) frames is
   exactly the committed line's payment. Third side-channel: `--tap-pref
   "turn:pre|post:idx,..."` — the scarcity greedy prefers the recorded-tapped battlefield indices
   in that (turn, phase) (rank −100000; ORDER bias only, never legality; enumeration and search
   run with the chooser nulled). Reconstructed by `recorded_tap_prefs` from the frame pairs (299
   pairs exist corpus-wide; every recorded frame carries `idx`). The fuel rank stays: it is
   defensible play (the band's own one-shot doctrine) and it covers s9_gi8's T3, whose payment no
   frame pair witnesses (single frame that turn — the divergence is only visible as the exiled
   fetch land next turn).

Result: BOTH FiveColour refs replay **ok** (recorded win turns, no repairs) under clean and under
the full bundle. All three fixes are inert outside reference replay (`--force-attackers` /
`--tap-pref` absent → natural behaviour; the fuel rank sits inside the `DorkTapLastEnabled()`
branch, so the clean engine is byte-identical).

**fix7 verification (2026-08-25):**
- VPC full corpus: **224/224 healthy under BOTH arms**, identical classes (130 ok / 94 repaired;
  `vpc_clean_fix7.txt` / `vpc_bundle_fix7.txt`). The one class change vs fix6 clean
  (StompySurprise s11_gi10 ok→repaired) is the tap pin being MORE faithful — same recorded T6 win.
- Clean-env smoke: 42/42 byte-identical (0 configs changed).
- The fuel rank changes bundle play only on Deathrite boards (= FiveColour). Held-out re-measure
  (`arm_BUNDLE_fix7_fivecolour_overnight.txt`, wins compared): searched depths moved **−84
  loss-penalized vs the fix5 arm**, and every fuel-rank mover either returns exactly to GT or
  beats it (gi92 7→6, gi134 6→5, gi125 5→4, s6006-d5 gi84 unwon→T8 WIN). **Zero new
  slower-than-GT games**; the remaining slower set (gi341/162/179/324/391/191/141) all predate
  the fix and are already in the fix5 classifier (5 churn, 2 known PERSISTS).
- Train seeds (`arm_BUNDLE_fix7_fivecolour_regression.txt`): fivecolour now **5 faster / 0
  slower** vs GT at searched depths — and **fc98, the last fivecolour residual near-tie, is
  CLEARED** (gi98 matches GT everywhere). `viewer_validate_check.js` now consumes the resolver's
  emitted side args (al.side) instead of its own duplicate builder.

**The plan-RECONSTRUCTION tier (reconstruct from cast names, validate by application) was
designed but is deliberately NOT implemented.** The real case refuted it: CheckLine said the
recorded line was ILLEGAL on the diverged board, so no reconstruction could have applied it —
when a replay diverges upstream, the divergence must be pinned at its source (which the recording
turns out to contain), not papered over at the failing frame. Keep it as the fallback design if a
future case ever shows a legal-but-unenumerated recorded plan on a truly identical state; note
CheckLine cannot *advance* the game on `LegalNotEnumerated` today, so that tier would need an
engine seam too. Invariant actually shipped: a hand-played reference stays alive because every
recorded fact of the game — choices AND witnessed automatic decisions (mulligan, cast order,
attacks) — is replayed as recorded, so only a genuine rules/legality change can kill one.

## Rejected shapes — do not re-derive

§10 Gold-Rush-specific reserve override (⛔ in lump doc); `ShouldAttackWith`-gated release
(sign-inconsistent, broke the Grove ref); creature-before-more-flexible-land in any form (six
losses); rank-sorting the backtracker's candidates (+0.0050); per-deck on/off for §2a (USER: on
everywhere or off everywhere); TrickCastSensible clause-(c) next-turn-acceleration tightening
(2026-08-25: compensated for the BuildNonCreaturePool defect, cost mw74's speculative bank —
admission is not choice, keep (c) loose and the pools honest); MTG_PAYSAC_FAN_CREDIT fan-width
mint credit (refuted: the under-credit was never the blocker).

### Attacker-hold levers: measurement, contamination post-mortem, and the corrected mirrorwing record (2026-08-25, cont.)

The two attacker-hold levers the USER asked for ("reserve as many attackers as we can when
Craterhoof is going on the field", cast or cheated in) were measured on the train suite, and the
analysis surfaced — then resolved — a measurement-integrity incident that also corrects this doc's
own fix5 record.

**Corrected fix5 record.** `arm_BUNDLE_fix5_regression` (06:31) measured a TRANSIENT tree: it ran
while the later-reverted gate-holds ((c)-tightening / fan-trick hold) were still in the code. Its
mirrorwing numbers (d3_s2002 4.7400, "mw43 recovered") describe reverted code and are NOT the
final tree's. The final tree's mirrorwing == **fix4's, byte-for-byte across all five regression
configs** (verified by digest against `fix4_regression_wins`); every other deck byte-matches the
fix5 arm (stompy keeps the Lodge band fix, fivecolour keeps the fuel rank). Consequences:
- Mirrorwing train position is fix4's: d0 5.9450 (GT 6.0410), d3 4.7700/4.6800 (GT
  4.8150/4.7450), d5 4.7100/4.7000 (GT 4.7600/4.7600) — still better than GT across the board.
- The mirrorwing held-out number that stands is the FIX4 overnight's −0.061 (that arm measured
  the same play; the fix5 overnight's mirrorwing block inherits the transient-tree caveat).
- mw43-class (§2a fan-trick near-tie) is OPEN again in train (gi43 d3/d5 s2002 = 5 vs GT 4) —
  it was never durably recovered; the "recovery" was the reverted admission gate. It rejoins
  mw148 in the residual near-tie set, same class, same three-strike conclusion (honest
  accounting, not admission gates).

**Contamination post-mortem (two chimera arms).** The first lever bisect run
(`arm_BUNDLE7rung_regression`, 08:27) reported mirrorwing == fix5-transient behaviour
(4.7400/15aa62a6) while its fivecolour was fix7 — impossible for one binary. A stale concurrent
process (fix5-transient-era code) interleaved into the SHARED `test/logs/regression/` logdir
(mtg.run / batch.log / wins are last-writer-wins). A verbatim re-run
(`arm_BUNDLE7rung_regression_RETRY`) reproduces standalone-batch results exactly and supersedes
it. LESSON (now a memory too): never run two regression.sh instances concurrently — the logdir is
shared mutable state; before trusting a surprising arm, re-run it or reproduce one config
standalone (`--batch` on the same manifest entry must byte-match the arm).

**Lever attribution (clean, vs the correct current-tree bundle baseline
`bisect_rc/base6_full_wins`, all on one binary):**
- `MTG_TAP_ATTACKER_RUNG`: searched movers ONLY on stompy — gi226 5→4 at BOTH d3 and d5 (the
  Craterhoof-class gain, the USER's reserve rule paying off), gi202/gi203 5→6 at d3 only
  (d5 recovers both). Searched net 0; d0 churn −4. Mirrorwing untouched.
- `MTG_DORK_HOLD_PARTIAL`: **zero searched movers on any deck**; d0 churn −5. Play changes at
  identical scores (digests move), so it is live but score-neutral on train; its origin case
  (StompySurprise gi47 margin-1 Craterhoof kill) is not in the train suite.
- Effects compose additively (BUNDLE8 = RUNG + PARTIAL exactly). The earlier "mirrorwing
  uniformly worse (+14)" reading was the fix5-transient baseline error, not the levers.

**Held-out (stompy overnight, `arm_BUNDLE8_stompy_overnight` vs the fix5 stompy-overnight
baseline):** searched **−195** (net −190): gi84 s6006 goes unwon→T8 WIN at d3 AND d5, 16 more
games a turn faster, 2 a turn slower (both d3-only). Only 2 NEW slower-than-GT games and both are
COVERABLE — gi831 s6006 returns to GT at the d5 case budget, gi767 s4004 at 16x/unbounded budget
(budget-churn class). **The USER's adoption bar ("average better, no uncoverable earlier
win-turns") is met.** The held-out bisect (`arm_BUNDLE7rung_stompy_overnight`) shows RUNG alone
reproduces every score exactly (remaining diffs are digest-only play detail): all measured gains
belong to `MTG_TAP_ATTACKER_RUNG`; `MTG_DORK_HOLD_PARTIAL` is score-inert in every measurement
(train, all decks; held-out stompy) — its case rests on the origin trace (StompySurprise gi47
margin-1 Craterhoof kill) and the USER's doctrine, not on suite numbers. Adoption remains the
USER's call.

### mw43 FIXED, mw148 reclassified (2026-08-25, cont. — the USER's unbounded-recovery rule)

USER rule (2026-08-25, standing): **a regression must win at unbounded budget with depth spanning
the original win turn; target ZERO unrecoverable regressions** (cases that make this impractical
get discussed, not silently tolerated).

**mw43 — root-caused and FIXED (commit `cb46b356`).** Not a near-tie after all: the §2a mint
credit leaked into `FillScaledXTrick`'s surplus, sized Libation to X=2 against a Treasure that
cannot exist at payment time, and the payment tapped the exalted Hierarch — the attacker the fan
needed. Dropping the credit from the FILL (payability keeps it) returns mw43 to GT T4 at both
depths and improves gi13 (s3003) 6→5 at both depths as a side effect; movers are mirrorwing-only,
searched net −4, clean smoke byte-identical. Diagnostic route for the record: `MTG_DUMP_EWINS`
(the clairvoyant oracle capped at 5 under §2a alone → mechanical block, not preference) → flag
bisect (eight minus-one arms) → claude-play forced walk of the winning line (frozen at the T4
attack: "Mirrorwing Dragon (15)" alone, Hierarch tapped) → `FillScaledXTrick`. The earlier
"three-strike / plan-preference" classification of mw43 was WRONG — it was an honest-accounting
bug all along, exactly the class the three-strike rule says to fix.

**mw148 — reclassified: the win is invisible to BOTH arms' searches.** `MTG_FSW_TRACE` at T4
shows no candidate with tail=4 in EITHER clean or §2a — both value their best plans at 5. Clean's
recorded T4 win emerges at EXECUTION: its chosen plan opens a draw breakpoint (Impolite Entrance
first), the mid-turn re-solve sees the freshly drawn Luxurious Libation, and the executed
continuation over-delivers (23 damage) relative to its searched value. §2a's §2a-priced plan
choice (same searched value 5, different first cast → different breakpoint) does not stumble into
that fortune. Attribution: §2a core (fresh-hold off doesn't restore it; bundle-minus-§2a does).
A fan-width mint credit (width = 1 + other own creatures when the mint targets a live magnet) was
built and REVERTED — it did not flip mw148 and had no other measured effect. Because no search at
any budget/depth values the 4 in either arm, budget/depth escalation cannot recover it — this is
the "impractical" discussion case the USER's rule anticipates. Candidate real fix (unbuilt):
value draw-breakpoint continuations against the post-draw hand in the bp-variant search wide
enough to surface the fan chain — a search-fidelity project, not an accounting fix.

**Bar position after the fix:** train (smoke + regression) uncoverable-earlier-win-turn set =
{mw148} only. Everything else: coverable churn (mw56, st202@d5, st203, st252, st292, gi767@16x,
gi831@d5) or shuffle variance (cg30, fc96).

### Held-out overnight (mirrorwing+stompy) under the full bundle — and the continuation-traits lockstep bug (2026-08-25, cont.)

**Measurement (BUNDLE9 = all 8 levers + the mw43 fill fix, mirrorwing+stompy overnight, 24 cells,
24,800 games; arm `logs/overhaul_sweep/arm_BUNDLE9_mwst_overnight.txt`, wins in
`bundle9_mwst_overnight_wins/`).** Every one of the 24 cells improves its average (d0, d3 and d5
alike; e.g. mw d3_s4004 4.7800→4.7275, st d3_s5005 4.9550→4.8320). Per-game (loss=99):
**2515 better / 301 worse**. The USER rule was applied to all 34 SEARCHED slower games (d0 is
greedy — outside the rule): pooled repro at case budget + unbounded probes
(`bundle9_probe_manifest.json`, 68 jobs). 23/34 coverable unbounded — including all three
newly-unwon d5 games (unbounded wins them 2 turns faster than GT). 11 uncoverable → minus-one-flag
bisect over the 8 levers (`bisect/cfg_*.log`):

| cluster | games | rescued by removing | class |
|---|---|---|---|
| A | mw gi43 (d3+d5), gi242 (d5, 5→8), gi292 (d3+d5) | any of TPS / OSR / DTL | FIXED below |
| B | st gi374 (d3+d5), gi993 (d3) | DORK_TAP_LAST alone | OPEN |
| C | mw gi136 (d5) | TREASURE_PAY_SOURCE alone | OPEN |
| D | mw gi326 (d3) | only all-off | OPEN (multi-lever) |
| — | mw gi196 (d3) | nothing (all-off unbounded=5 too) | NOT lever-caused: pre-existing depth non-monotonicity; the GT config reproduces GT 4 exactly |

**Cluster A root cause — continuation-traits lockstep hole (FIXED this commit).** Microscope on
mw gi43 (route: game-log diff → EWINS → FSW → `[fd]` committed line → new `MTG_TRICK_TRACE`):
the committed T5 line is `Gold Rush` + site-5 breakpoint continuation `[Fortifying Draught]`,
scored win=5 = 12 exact lethal (6 bodies + GR +4 and Draught +2 concentrated on Elvish Mystic #1,
Draught paid by cracking the banked Treasure so Mystic #1 swings). The brace depths told the
story: ApplyPlanDirect's `PlanTraitsScope`/`TapKeepLastScope` close with the main cast section
(depth 2), so the ROLLOUT applied the deferred continuation TRAIT-LESS — no one-shot hold, §2a
rank-26 cracks the Treasure eagerly, Mystic spared. AIEngine::TakeTurn's twin scopes span the
whole turn (depth 1), so the EXECUTOR replayed the same continuation under the MAIN plan's LIVE
traits — OneShotHoldMask held the Treasure and the payment tapped Mystic #1, the pump target:
`[trick] T5 Fortifying Draught -> Elvish Mystic#1(tapped)`. 12 scored, 5 executed, win 5→7.
Fix: the continuation is its own mini-plan — BOTH sides now install
`ComputePlanTraits(state, continuation.actions)` (+ keep-last) around the continuation apply
(TurnSolver deferred loop; AIEngine resolve_draw_breakpoint + replay_recorded). Null scope
(levers off) unchanged. Also kept: the mint-credit fresh-hold parity gate (a magnetless mint is
banked and cannot fund this turn — credit only with a live magnet), the same phantom-credit class
as the mw43 fill fix. Result: all five Cluster-A entries at-or-better-than GT (gi43 → 5 beats GT
6 at both depths; gi242 → 5 = GT; gi292 → 6 beats GT 7 at both depths).

**Diagnostic added:** `MTG_TRICK_TRACE` (default off, real-resolution only) — dumps where each
solo-target trick payload landed (target, pump, treasure count).

**Cluster B first look (st374):** divergence at T1 (all-on skips the T1 Llanowar Elves the win-4
line needs). `[fd]` shows win-4 Llanowar-first candidates being demoted to 5 on committed-line
verification — the same score-vs-replay divergence class at a different site, DTL-conditioned.
Root-cause pending (next session): same route as gi43.

**BUNDLE10 validation (fixed tree, same 24-cell overnight; `arm_BUNDLE10_mwst_overnight.txt`,
wins in `bundle10_mwst_overnight_wins/`).** Smoke (levers off) 42/42 byte-identical. Levers on:
every mirrorwing cell improves further vs bundle9 (e.g. d5_s4004 4.7267→4.7067); stompy cells
byte-identical to bundle9 (no treasures — the fixes are inert there). vs GT: 2509 better / 276
worse (was 2515/301); 66 games moved vs bundle9, mirrorwing only. USER-rule re-probe of all 26
searched slower games (`bundle10_probe_manifest.json`): 20 coverable (including the one new
mover, mw d3_s6006 gi209 5→6, unbounded=GT), **6 uncoverable = exactly the open clusters**:
st374 d3+d5 / st993 (B, DTL), mw136 (C — rescued by `MTG_PAYSAC_FRESH_HOLD=0`, so fresh-hold
class like Cluster A but NOT via the continuation scope; site-5-off does NOT rescue it), mw326
(D, multi-lever), mw196 (non-lever, pre-existing). Payment worklist remaining: B, C, D.

## Cluster B root-caused: two mechanisms, neither a payment-legality bug (2026-08-25, session 2)

Both stompy uncoverables (gi374 d3+d5, gi993) are DORK_TAP_LAST-conditioned, but by different
routes, and in both the bundle's PLAY is right or better — the loss happens a layer above payment.

**st374 — the greedy-bottoming oversized-hand illusion (pre-existing; DTL exposes it).**
The arms diverge at the mull-3 BOTTOM choice, not in play. `AIEngine::BottomCards`' clairvoyant
lookahead evaluates each candidate removal on a hand one card too big (at step 2 of 3 the trial
plays 5 cards). Under DTL that oversized trial finds a REAL win-4 — T2 Llanowar chip (DTL taps
Forests for Priest, keeping Llanowar to swing — exactly the lever working) + T3 Natural Order →
Terastodon destroying OUR OWN three Forests for three 3/3 Elephants + T4 alpha 9+3+3+3+1 = 19 =
exact lethal after the chip — but that line needs ALL FIVE cards; no legal 4-card keep realises
it. "Bottom Sol Ring" therefore ties at 4 in step 2, the heuristic picks it, and the only real
4-line (Sol Ring → T2 NO) dies one step later when everything scores 5. The DTL-off arm plays the
trial WORSE (taps Llanowar for Priest's cost with two Forests open — the reservation defect this
overhaul fixes — losing the chip), scores it 5, and keeps Sol Ring by accident. Verified by
replaying the 5-card trial as a real game via `--force-mulligan "3:53,43"`: DTL-on wins 4,
DTL-off wins 5. Fix shape: evaluate bottom candidates at LEGAL final hand size (heuristic-bottom
the remaining count inside each trial before the rollout; rollout count unchanged). That is an
unconditional engine change (clean-GT churn), so it goes behind a temporary A/B flag and adopts
with the flip's single rebaseline.

**st993 — the SPB attack-rung's un-valued scaler tap (SCALER_PLAN_BIAS × DTL).**
Mull-1 (legal trials); the play itself. On the T4 lethal-alpha turn the winning plan is NO-only
(4 mana = 3 lands + one dork pip). The attack-turn scaler bias ranks scalers F=64 ahead of flat
dorks F+1=65 ("one big body pays what N flat bodies would"), so the per-pip greedy taps Elvish
Archdruid — yield 3 for a 1-pip need (waste 2), body worth 5 under Craterhoof — while Arbor Elf
(yield 1 via the untap trick, waste 0, and the NO sac fodder = zero body cost) sits idle. Alpha
13 < 16 → win 5. Forced-walk proof (claude-play `--choices "0,0,46,-1,150,-1"` +
`--force-mulligan "1:5"`): full bundle → Priest+Hoof = 13 (opp 3); identical walk with
MTG_SCALER_PLAN_BIAS=0 → Archdruid+Priest+Hoof = 18, opponent -2, WIN 4. (Autonomous minus-SPB
does not rescue the full game — earlier turns shift — which is why the suite bisect read
"DTL alone".) Fix shape, per §2b "waste is the trigger" and the USER's reservation doctrine
("tapping the Llanowar first is good when we need its mana, bad when we don't", 2026-08-25):
scaler-first only when the remaining need ≥ the scaler's yield; when a flat band-mate covers the
remaining pips exactly, flat taps first. Lever-gated — no clean-GT churn.

Open sub-question found on the way: the TAP_ATTACKER_RUNG / partial-hold ladder should have held
Archdruid at that T4 and did not — suspect TapForCostBacktrack cannot sequence Arbor Elf's
untap-trick (its yield needs a tapped Forest at fire time), making every scaler-held rung read
infeasible on untap-trick boards. Verify before trusting the rung on such boards.

### Cluster B fixes: built and locally verified (2026-08-25, session 2 cont.)

**Fix 1 — waste-aware scaler rung (in-tree, lever-gated).** `TapForCostSharedOnce`'s greedy now
publishes the payment's per-pip remaining need net of floating (`g_pay_remaining_pips`,
SpellEffects.h; 0 everywhere else = every other reader unchanged), and the SPB attack-turn branch
in `GenericProvider::ManaSourceRank` demotes a scaled dork behind the flat band whenever the
remaining need is smaller than its yield. Verified on the st993 forced walk: the T4 Natural Order
pip now taps Arbor Elf (the sac fodder) instead of Elvish Archdruid — alpha 13 → 18, executor
kill restored. Inert with levers off (smoke 42/42 byte-identical).

**Fix 2 — legal-size bottoming subset table (`MTG_BOTTOM_LEGAL`, default OFF pending A/B).**
`BottomCards` under the flag rolls out every legal removal subset once (C(h,count) ≤ 35,
shared-TT amortised) and scores each per-step candidate as the best win over subsets consistent
with the bottoms so far; the heuristic tiebreak among allowed candidates is unchanged. A first
attempt that heuristically bottomed the remainder inside each trial was REFUTED (the standalone
heuristic bottoms payload; trials collapsed to 7s/9s) — do not rebuild it. Verified: st374 →
win 4 at BOTH shapes (d3 case budget 20ms and d5-shape unbounded), = GT; flag off → 5
(shipped behavior intact); smoke 42/42 byte-identical.

**st993 residual (OPEN — discussion case).** With both fixes the autonomous game is still 5.
The [t4dbg] node dump proved the loss is not payment or combat: under DTL the T3 chip is
BIGGER (3 vs off-arm 2), both T4 states are kill-capable, and the forced walk executes the kill —
but the T2-line search never SIMULATES a T4 Natural Order node from the {Archdruid, Priest}
board (the off arm reaches it exactly once and wins). Not depth-coverable (d4/d5 unbounded = 5).
Negative probes (do not re-run): every single minus-lever except DTL, LEGACY_SEARCH,
FD_ALWAYS_RESEARCH, BP_SEARCH=0, DORK_ATK_HOLD_DIR=0, DORK_ATK_SEARCH=0, FORCE_HOLD_TURN=3/4,
TAP_SCALED_LAST=1, TAP_POWER_ORDER=1. Classification: a search-frontier expansion boundary
flipped by the one-point opp-life delta the (better) DTL chip creates — the gi196 family
(pre-existing expansion non-monotonicity), surfaced-not-caused by the lever. Candidate for the
USER's "impractical cases get discussed" clause rather than a payment-honesty fix.

### Fix 1 REFUTED by measurement and reverted (2026-08-25, session 2 cont.)

The waste-aware scaler demotion (per-cast remaining-pips vs scaler yield) measured NET WORSE on
held-out stompy overnight: the fix1+fix2 arm was 111 slower / 23 faster per-game vs bundle10, and
the single-cell bisect (stompy d3 s4004, 1000g) attributed it to fix 1 alone — bundle10 4.8410,
fix1-only 4.8620, fix1+fix2 4.8580, post-revert flag-off 4.8410 (byte-exact bundle10 parity
restored). WHY it lost: the per-CAST view is myopic — the scaler's "wasted" surplus floats into
the turn's later casts (commit_leftover), so §5b's one-big-tap is correct at TURN scope and the
demotion instead tapped one flat body per cast tail-pip, bleeding attackers. The refutation note
is inline at the ManaSourceRank site. Retry directions if ever revisited (turn-scope only):
tap-the-sac-fodder-first (a body the plan sacrifices this turn pays for free — the true zero-cost
insight from st993's T4), or batch-combined remaining need. Neither closes st993 itself (its
residual is the search-frontier expansion boundary, already ledgered), so nothing payment-honest
is currently lost by the revert.

Fix 2 (MTG_BOTTOM_LEGAL) survives on its own: gi374 -> 4 at case budget post-revert, smoke 42/42
byte-identical, and the fix2-only stompy overnight arm (arm_BUNDLE12_fix2only_st_overnight.txt)
is the adoption measurement.

### Fix 2 held-out validation: strictly dominant (2026-08-25, session 2 cont.)

Fix2-only stompy overnight (arm_BUNDLE12_fix2only_st_overnight.txt, bundle + MTG_BOTTOM_LEGAL on
the reverted tree) vs bundle10, per-game across all 8 searched cells: **0 slower / 20 faster**
(every cell average improves; gi374 5->4 at d3_s6006 AND d5_s6006). Stompy d0 cells are
byte-identical to bundle10 (flag gated on lookahead bottoming), and mirrorwing was measured
byte-identical under the flag in the combined arm. The subset table also cleared the ORIGINAL
greedy-bottoming victims beyond gi374: gi176/gi679/gi896/gi232/gi731/gi905/gi299/gi779/gi968/
gi3/gi104/gi500/gi671/gi75 all faster -- the oversized-hand illusion was a standing tax on
stompy mulligan games, not a one-off.

### USER challenge overturns two classifications (2026-08-25, session 2 cont.)

The USER rejected "draws diverge" as an explanation ("we changed no search heuristic nor
randomness") — and the code agrees: SearchShuffleSeed keys on (game_seed, shuffle ordinal), salts
equal in normal play, so mid-game shuffles are DETERMINISTIC and the search's simulation of a
shuffling line reproduces the real post-shuffle order exactly ("they stay in lockstep",
SpellEffects.h ~1058). A shuffle is therefore fully priced by a search whose window spans the win
turn — "variance" was never a closed class. Depth-4/5 unbounded probes:

- **gi196 (mw overnight d3_s7007) RECLASSIFIED COVERABLE**: clean d4/d5 unbounded = 4, bundle
  (+BOTTOM_LEGAL) d4/d5 unbounded = 4. The earlier "pre-existing depth non-monotonicity" was a
  d3-horizon artifact — at d3 the T4 win sits at the tail boundary and the tail heuristic
  mis-prices it; at depth spanning the win turn both arms find it. Satisfies the USER rule.
  (Consequence: the "gi196 family" framing for st993 is RETRACTED — st993 does NOT recover at
  d4/d5 unbounded and stands alone as an unrecovered search-frontier case.)
- **cg30 (creature_giving train d3+d5 s3003) RECLASSIFIED OPEN DEFECT**: clean d3 unbounded = 4,
  bundle d3/d4/d5 unbounded ALL = 5. Since the post-shuffle future is faithfully visible to the
  search, the bundle arm's preference for the T2 Crop Rotation line (realizing 5) over the
  win-4 line is an evaluable, wrong choice at some layer — the same score-vs-realization or
  frontier class as the other clusters, NOT draw luck. Needs the standard route (minus-one
  bisect on cg30, then game-log diff / EWINS / fd). The fix5-era "proven fetch-shuffle
  variance" note stands only as a description of WHERE the games diverge, not as a benign class.

### Re-audit of the variance-waved set + cg30/fc96/fc341 bisects (2026-08-25, session 3)

Depth-spanning unbounded probes on every game previously waved off as fetch/shuffle variance
(current tree = bundle12 + MTG_BOTTOM_LEGAL; bundle vs clean, single games):

| game | shape probed | clean | bundle | verdict |
|------|--------------|-------|--------|---------|
| fc82 (overnight d3+d5 s4004) | d3 unb, d5 unb | 4 | 4 | RESOLVED (= GT) |
| fc350 (overnight d3 s5005) | d3 unb | 4 | 4 | RESOLVED |
| fc391 (overnight d3 s6006) | d3 unb | 4 | 4 | RESOLVED |
| ds227 (overnight d3 s5005) | d3 unb | 6 | 6 | RESOLVED (both beat GT 8) |
| fc96 (smoke s1001) | d3 unb, d5 unb | 4 | 5 | **OPEN DEFECT** |
| fc341 (overnight d3 s4004) | d3 unb, d5 unb | 5 | 6 | **OPEN DEFECT** |

Minus-one bisects (single-lever confirmations both ways):
- **cg30 = MTG_DORK_TAP_LAST alone** (minus-DTL 4, all other minus-ones 5, DTL-only 5).
- **fc96 = MTG_DORK_TAP_LAST alone** (same signature: minus-DTL 4, DTL-only 5).
- **fc341 = MTG_M2_RELEASE alone** (minus-M2R 5, all other minus-ones 6, M2R-only 6,
  DTL-only 5).

Game-log diffs (all three share one shape — an EARLY near-tie choice flips under the lever,
its fetch/CR shuffle reroutes the draws, and the realized game is one turn slower):
- cg30: T2 clean holds Crop Rotation; DTL casts CR#12 T2 → T3 draw flips BoP→Hunted Phantasm →
  only 5 mana T4 → Massacre Wurm slips T4→T5 (clean T4 Wurm ETB = exact −2 lethal).
- fc96: T1 clean plays Zagoth Triome (tapped); DTL cracks Verdant Catacombs + T1 Deathrite →
  draws reroute → Hellkite line lands T5 not T4.
- fc341: T1 clean plays Godless Shrine, T2 Foothills; M2R fetches T1 → never draws the 2nd
  BoP → Cosmic Spider-Man T3→T4, win 5→6.

**EWINS oracle result on cg30 T2 (the decisive fact):** clean arm — the hold candidate
(Orchard + Suture Priest, no CR) realizes win 4, earliest=4. DTL arm — the SAME hold candidate
realizes win **5**, earliest=5 (NO candidate reaches 4). Since EnumerateEarliestWins EXECUTES
continuations, DTL play loses the win-4 continuation DOWNSTREAM of T2 (T3/T4), and the T2 CR
cast is a mere tiebreak among all-5 candidates. This is why depth cannot cover it (bundle
d4/d5 unb still 5): the defect is not the T2 branch choice but the DTL play POLICY misplaying a
later turn on every branch. Next: claude-play forced walk of the clean line under DTL to catch
the leaking turn (suspects: CR sac/fetch choice, Orchard-tap count into the Wurm ETB
drip math, or the T4 {3}{B}{B}{B} payment).

### cg30 ROOT-CAUSED: a DTL search-vs-executor fidelity hole, NOT a tap-order preference (2026-08-25 s3)

Forced-walk proof (claude-play, DTL-only arm, exact autonomous opening incl. the Temple Garden T1
fetch; CSV `0,1,0,0,46,-1,0,0,0,0`): forcing the clean line — T2 play Orchard + cast Suture Priest
ONLY, T3 Misty + CR->Orchard + CR->Orchard + BoP (sacs = engine defaults), T4 Heath + Massacre
Wurm — the EXECUTOR reaches opponent **-1 by end of turn 4** under MTG_DORK_TAP_LAST=1. Every plan
chosen is in the engine's own enumerated plan list and every sub-choice is the engine default, so
this line is fully search-visible. `PlayOutFrom` checks `CheckWinCondition` after every turn ->
the real game would record WIN 4. Yet `EnumerateEarliestWins` at the same T2 state (deep exact
search, unbounded) reports earliest=5 with NO candidate at 4, and the autonomous DTL game plays 5.

Therefore cg30 is a **search-simulation fidelity hole under DTL** (the search's simulated
continuation loses damage the executor actually deals), same FAMILY as Cluster A's
continuation-traits lockstep hole — not the Orchard-rank/band-design issue previously sketched.
The deck mechanism that makes the game sensitive: every Forbidden Orchard tap with Suture Priest
out = 1 drip + 1 opponent Spirit that Massacre Wurm's ETB converts to 2 more damage; the win-4
line's margin lives in those token-drip chains, so a simulation that mis-taps or mis-credits one
Orchard tap flips 4->5. Next diagnostic: MTG_FSW_LINE on the T2 hold plan to dump the search's
simulated T3/T4 and diff it against the executor walk above (suspects: the search's per-cast
payment under DTL sacking/tapping an Orchard the executor keeps, or the payment-Spirit Priest
drip not credited in the search's fast path).

Same-arm observations logged on the way:
- **fc96 = same DTL signature at the choice level** (EWINS T1: the Zagoth-hold candidate itself
  degrades 4->5 under DTL; earliest=5) — likely the same fidelity class, verify after the cg30 fix.
- **fc341 = MTG_M2_RELEASE single-lever** (minus-M2R=5, M2R-only=6); divergence T1
  Godless-Shrine-vs-fetch near-tie, realized 5->6; not yet root-caused.
- **claude-play bottom decision: `ai_choice` != `ai_set`** (cg30 mull-1: ai_choice says bottom
  Massacre Wurm idx 3, ai_set=[0]=Soul Warden which is what the autonomous engine actually
  bottoms — following ai_choice does NOT reproduce the autonomous opening, contradicting the
  skill's claim; use ai_set. Worth a look + a skill doc fix.)

### cg30 FIX BUILT, VERIFIED, COMMITTED 94653ab3 (2026-08-25 s3)

`MTG_SAC_SPAWN_LAND_LAST` (DEFAULT OFF, joins the adoption config): SacrificeLandCandidates
ranks a `taps_spawn_opp_token` land after every fungible land (tapped-first within bands). TWO
code changes were required — the provider rule, AND the executor's autonomous cast path
(AIEngine ~4295), which open-coded its own "first tapped land" duplicate that never consulted
the provider. That duplicate was itself a live lockstep hole: after the provider-only fix, the
SEARCH found the win-4 hold line (EWINS T2 earliest 5 -> 4) while the EXECUTOR still ate the
Orchard and realized 5 — the two-sites failure mode of the coding conventions, caught in vivo.
The executor site now routes through the provider (byte-identical order with the flag off).

Verified: cg30 bundle+lever = 4 at d3/d4/d5 UNBOUNDED and at CASE budget (was 5 at every
shape); DTL-only+lever = 4; lever-off bundle = 5 (lever is the difference both ways); clean
unchanged 4; clean-env smoke 42/42 byte-identical (the executor refactor is order-preserving);
lever inert on FiveColour (param-gated — no spawn lands). fc96/fc341 remain open (different
mechanisms: fc96 = DTL on FiveColour, no spawn land involved; fc341 = M2_RELEASE).
creature_giving train cell re-measure vs bundle12 (bundle13_saclever_cg_regression_wins):
SEARCHED **5 faster / 0 slower** (gi30 d3+d5 5->4 = the fix; gi38 d3+d5 5->4; gi272 d3 5->4);
d0 greedy 11 faster / 1 slower incl. gi475+gi949 unwon->T8 wins; d0 loss-penalized avg
5.586 -> 5.576. Lever byte-inert on all other decks (no taps_spawn_opp_token card).

### fc96 dig (2026-08-25 s3, partial — root-cause bounded, not yet fixed)

Forced-walk proof (claude-play, DTL-only, CSV `1,14,7,0,2,-1,0`, seed 1097 gi96 d3): the executor
realizes **win 4** playing clean's exact line — T1 Zagoth (hold), T2 Catacombs->Godless Shrine +
DRS + BoP, T3 Heath->Stomping + Bloom + Faeburrow, T4 Hellkite pre-combat, attack 10 (Hellkite 5 +
vigilant 5/5 Faeburrow), then post-combat **land drop of the attack-drawn Scalding Tarn** + Unite
the Coalition x=5 (7 mana: Faeburrow's 5-colour tap + DRS + the Tarn). The DTL deep search
(EWINS T1, unbounded) says earliest=5.

Mechanism as bounded so far:
- Clean's dork-first Hellkite payment leaves the LANDS untapped -> post-combat pool reaches
  Unite's 7 without any m2 land drop -> clean search finds 4.
- DTL's lands-first payment leaves only Faeburrow+DRS (6) -> the m2 Unite needs the 7th mana from
  the attack-drawn fetch, and the search's second main is CAST-ONLY by default: the m2 land drop
  is `Main2DropEnabled()` = **MTG_MAIN2_DROP, DEFAULT OFF** (EngineFlags.h — a parked adoption
  item: "on adoption flips default-ON with MTG_NO_MAIN2_DROP hatch + GT rebaseline"). The
  autonomous executor's greedy m2 land play is suppressed at depth>0 for the same reason
  (AIEngine::TakeTurn gi=141 note).
- HOWEVER MTG_MAIN2_DROP=1 does NOT rescue fc96 (DTL+drop still 5) — the remaining blocker is
  inside the m2 machinery (suspects: the m2 drop path not CRACKING a fetchland, or the m2 pool
  estimate; also note no traced m2 node scored sub=4 in EITHER arm while clean's EWINS says 4,
  so the winning path routes around the [m2t]/[fsw] traced sites — instrument the collapsed-main
  route next).
- Negative probes: MTG_ESC_BEAM=0 still 5 (not beam pruning); sim combat DOES attack with the
  vigilant Faeburrow when untapped (cbdbg 19->8 node exists under DTL); Spirit/vigilance
  modelling not implicated. MTG_SAC_SPAWN_LAND_LAST inert (no spawn lands).
- fc341 (M2_RELEASE single-lever): M2R+MAIN2_DROP still 6 — same family suspicion (second-main
  machinery), undug.

### fc96 dig, session 4 (2026-08-25 s4) — every layer but one ELIMINATED; the defect is
### "pre-combat payment is blind to the post-combat payload"

Instruments this session (all temp, reverted; artifacts in logs/overhaul_sweep/sac_axis/):
[m2t] NODE census + WIN-THIS-TURN print at the FSLineTail second-main early return
(fc96_m2drop_diag*.err), [cbt] sim-combat census with per-site tags + Faeburrow
vigilance/tap state (fc96_cbt*.err), [pay] both-world payment tap trace keyed on the
Hellkite cost {1}WUBRG (fc96_paytrace*.err), [shuf] shuffle-ordinal trace (fc96_shuf.err).

Corrections to the s3 bound (card data re-read — the recall trap the claude-play skill
warns about, again):
- Two-Headed Hellkite is {1}{W}{U}{B}{R}{G} (6 MV, one pip of each colour), NOT {3}{R}{R}.
- The committed bundle line at T4 is "cast Hellkite AND Garth One-Eye" — Garth {WUBRG} is
  paid by Faeburrow's 5-colour burst, which TAPS Faeburrow, which is why the committed
  combat is only Hellkite+DRS (6 dmg). The walk's winning deviation is exactly "HOLD Garth
  this turn": Faeburrow stays untapped, attacks (vigilant 5/5), combat = 11, and the m2
  casts Unite off Faeburrow+DRS+the drop.

ELIMINATED this session (each with direct evidence):
- Sac axis / spawn lands: MTG_SAC_AXIS=1 inert on fc96 (no sacrifice_land spells). Still 5.
- Attack declaration: an untapped vigilant Faeburrow DOES attack in sim combat — 474/524
  [cbt] nodes with fae=VIG untapped declare it. The vigilance exemptions exist at BOTH hold
  layers (HoldManaSourceForCollapsedMain under DorkAtkSearchEnabled default-ON;
  FiveColourProvider::ShouldAttackWith first check) and the card data carries Vigilance.
- Summoning sickness across sim turn boundaries: SimulateEndAndStartNextTurn clears
  entered_this_turn (TurnSolver ~16271). Hellkite attacks in-sim via Haste (keyword).
- Payment: the payer credits domain bursts correctly (real T4 [pay]: Garth {WUBRG} paid by
  ONE Faeburrow tap amt=5; Bloom tapped for G floats its surplus) and the ROLLOUT's dominant
  Hellkite payment (607x) is byte-identical to the executor's (Godless,Zagoth,BoP,Stomping,
  Bloom — Faeburrow AND DRS left up). The reserve masks were rmask=0 throughout (no
  reservation interference).
- Shuffle/draw determinism: [shuf] shows perfect CRN alignment — every shuffle at ordinal 0
  yields top=Bolas|Garth, every ordinal-1 shuffle yields top=BoP|Mana Cannons, across real
  play and every sim line at any turn/libsize. Draws do NOT diverge.
- Commit-the-line: MTG_LEGACY_SEARCH=1 (per-turn SolveWithLookahead, no committed replay)
  reproduces the exact split — bundle 5, clean 4. Not a commit/replay artifact.
- MTG_FD_ORACLE: no divergence flags (the committed 5-line realises its predicted 5).

THE MECHANISM, now precise:
  clean (dork-first): T4 Hellkite payment taps dorks, leaves the 3 lands + Faeburrow -> the
  post-combat pool reaches Unite's {2}WUBRG=7 WITHOUT any land drop -> search finds 4.
  DTL (lands-first): the same plan's payment leaves exactly Faeburrow(5)+DRS(1)=6 < 7 ->
  the only rescue is the m2 drop of the attack-drawn Tarn (-> Breeding Pool, +1 = 7), i.e.
  the executor's realized walk line. MTG_MAIN2_DROP=1 still scores 5; the [m2t] NODE census
  under it shows nodes at oppL=8 with land_plans>0 but unite_plans=0 — on-path prefixes
  reach the right life total but their m1 payment (plans casting more than Hellkite, or
  eating DRS for a generic pip) never leaves BOTH the 6 dork mana AND the drop-completable
  7th, so Unite is correctly excluded by affordability at every enumerated node. Zero
  on-path (site=1) T4 combats ever occur with the real library AND Faeburrow untapped: the
  solver never simulates the exact winning T4 shape because the shapes it enumerates are
  priced under the DTL payment, which forecloses the m2 before the m2 is ever asked.

CLASSIFICATION: not an m2-machinery bug and not a payment-legality bug — a PLANNING
blindness: the pre-combat payment (and the plan pricing built on it) never sees the
post-combat payload (Unite, {2}WUBRG, 7) sitting in hand, so under DTL's fixed lands-first
order it spends the exact sources the m2 needs. The overhaul's own reserve layer
(ONESHOT_RESERVE / M2_RELEASE rungs) is the architecture for "leave up what a later phase
needs" but has no rung for "reserve toward a castable post-combat payload". Candidate fix
shapes (user decision):
  (a) an m2-PAYLOAD RESERVE rung: when the hand holds a payload castable off (post-combat
      pool + planned drop), bias/deviate the m1 payment to keep it payable — the (c)-style
      whole-turn reserve, phase-aware;
  (b) a searched payment-order axis for the contested case only (audit doc item 4 — wide);
  (c) accept DTL's cost here and let fc341's dig decide whether the family is one defect.
fc341 (M2_RELEASE-caused, same second-main family) should be re-diagnosed with THIS lens
first — its bisect lever differs but the shape (post-combat payload priced out by an m1
decision) may be identical.

### fc96 FIXED (2026-08-26, commit 0e35cde4): MTG_M2_PAYLOAD_RESERVE

USER ratified the payload-reserve shape ("this is clearly correct") with ONE caveat, now
encoded in the design: never rule-defer Garth to m2, because a Lightning-Greaves-hasted m1
Garth attacks (and activates); the reserve therefore only re-picks WHICH legal payment is
committed — every cast line (including hasted-Garth-m1) stays enumerated and win-turn
selection owns the phase decision.

Build lessons (both caught by the acid loop, both now comments in the code):
1. Anchor = the VIGILANT domain dork, not max-yield-first-index: anchoring Bloom pushed
   Faeburrow into the m1 payment and forfeited its attack (still 5).
2. Payload = highest-MV DAMAGE-ON-CAST card, not highest-MV: max-MV picked Nicol Bolas (8)
   over Unite (7), and a walker converts to nothing the turn it lands (still 5, 775 wrong
   T4 reservations traced).

Measured: fc96 = 4 at d3 and d5 (bundle+lever; was the last DTL-caused unrecoverable);
cg30 still 4; clean smoke 42/42 byte-identical (dormant); FiveColour train bundle-vs-
bundle+lever 1600 games ZERO win-turn changes; structurally inert without a domain_mana
anchor. fc341 (M2_RELEASE, 6 vs clean 5) NOT rescued — different mechanism, still open.
Lever default OFF; joins the adoption-config candidate list (now: 8 levers +
MTG_BOTTOM_LEGAL + MTG_SAC_SPAWN_LAND_LAST + MTG_SAC_AXIS? + MTG_M2_PAYLOAD_RESERVE —
axis default and payload default are USER decisions at the flip).
