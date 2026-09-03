# Search recoverability audit — what high-depth/high-budget can and cannot fix

**Status: FULL INVENTORY (2026-09-03, five-way classification fan-out + spot verification).
Charter from the USER, same day:** *"recoverability is a crucial aspect of my design here. When
things get non-obvious I want to have the option to run high-depth high-budget and be confident
we don't have a bunch of lossy logic messing up the search."* The invariant: **the search must
converge at the limit** — any logic in the searched structure whose error does NOT vanish as
depth/budget grow violates the design regardless of measured frequency at shipped settings.
The corollary ruling (2026-09-03): playout-layer flaws (greedy rollout policy, weak heuristics,
mana-tap rules) are acceptable BECAUSE budget dilutes them; searched-structure flaws are not.

**REFINEMENT (USER, 2026-09-03):** *"It's not an issue that there are some heuristics (like the
discard one) that cut out lines unless they are cutting genuinely better lines"* — better lines
*"that are accessible without clairvoyance."* So the bar is NOT "never drop a line" — it is
**"never drop a line that is STRICTLY BETTER on information the player legitimately has."** A cut
is acceptable when the dropped lines are equivalent-or-worse (dominance, equivalence-class
representatives, provable infeasibility), AND a dropped line that only wins via knowledge the
engine should not have (exact future draws / shuffle order) does not count against the cut.
It violates the invariant only when the "this line is worse" judgment can be WRONG on legitimate
information. Consequently a DROP-hard classification below is not by itself a violation — the
question for each is *can the cut line be strictly better than every kept one, non-clairvoyantly*,
and the verification instrument's comparison is OUTCOME-based (does lever-off at unlimited budget
ever win earlier, or win where the shipped structure loses) with a clairvoyance check on any hit.

**OPERATIONAL BAR (USER, 2026-09-03, later the same day):** *"I'm mostly only worried about cases
that actually make a FASTER line unreachable. If they don't have a way to do that, then it's fine
to move on for now"* — faster **non-clairvoyant** lines in particular: *"I'm not worried about
clairvoyance cases, because true play quality is the goal."* So the triage question per entry is
exactly: **can this mechanism make a faster line unreachable on information the player
legitimately has?** Mis-SCORING with the candidate set intact (the escalation beam, the
ROOTTURN/site-3 evaluator greedy) moves OFF the active list — those are quality/compute calls,
not reachability cuts. Any probe hit must pass the clairvoyance attribution check before it
counts (the clairvoyance-vs-R-noise discipline).

**RESTORATION CONSTRAINTS (USER, 2026-09-03, working through the list):** (1) *"not to drop any
heuristics that are saving significant work without an alternative for performance and budget"*
— a work-saving gate with a confirmed reachability hole gets a TARGETED rescue (the EF
rescue-only pattern), never deletion; (2) *"if we can determine that a heuristic falls short
only because of clairvoyance we can leave it be"*, and the reason is load-bearing: *"we don't
want to have to design any heuristics with clairvoyance in mind"* — beating a clairvoyant line
would require the heuristic to encode hidden information (the exact next draws), which is a
design anti-goal, not an omission. So a probe hit that is an isolated draw-specific flip is
attributed to clairvoyance and closed; only systematic, draw-robust improvement indicts a gate.

Classification legend: **IDENTITY** (provably equivalent / sound necessary condition),
**ORDERING-ONLY** (recoverable by construction), **DROP+REOPEN** (budget-guarded wave/tranche
re-opens it, truncation counted), **DROP-hard** (no reopen — judged case-by-case under the
refined bar), **PLAYOUT-EXEMPT** / **USER-EXEMPT** (rollout policy; attacks + per-cost mana
allocation). Items marked **VERIFIED** were re-checked in source by the integrating agent;
the rest are fan-out code-reading claims with cites.

---

## 0. Defects FIXED during this audit (2026-09-03)

* **Land-Aura B&B under-count (VERIFIED + FIXED).** `78a4af94` taught the tap-backtracker's DFS
  to credit an attached Wild Growth/Overgrowth when the host taps, but neither `SourceMaxNet`
  overload nor the B&B `source_max_net` lambda carried the bonus — so the gate documented as
  "deliberately over- (never under-) counts so the gate stays lossless" could under-count an
  enchanted land by 1–2 and prune a payable cost (the storage/scaled/domain lesson again). Fixed:
  `source_max_net` now adds `LandAuraBonus` (SpellEffects.cpp ~1580). Blast radius before the
  fix: the backtracker is the sole payer for whole-turn prepay, the per-cast complete fallback,
  and both enumerator rescues.
* **Batch-arm cache leak (VERIFIED + FIXED).** The bp-enum plan cache and the canon verdict memo
  are `thread_local`, survive the batch runner's job switches, are NOT in `ClearPerGameCaches`
  (which clears exactly solve memo ×2 + enum memo), and folded no heurarm lever — so a MIXED-ARM
  pooled batch shared enumeration entries across arms whenever a lever changes enumeration output
  (`MTG_CONDEMN_M1_BP`, `MTG_SUBSET_ROCK_COLOR`). This is the unfixed second instance of the
  `batch-pool-contamination.md` class. Fixed: `BpEnumBuildKey` folds the whole per-job arm
  (all-unset folds a constant ⇒ byte-identical outside mixed-arm batches). Committed GT was never
  at risk (single-config runs). NOTE: the 2026-09-02 adoption gates used mixed-arm batches, but
  the recipe's levers (SITE3/NODE/CANON_*) select hosting/continuations rather than changing the
  enumerated list per state, so contamination there is believed inert — flagged, not re-measured.

## 1. Recoverable BY DESIGN (budget lifts the cap) — with the holes now mapped

* **Breakpoint width W=2 + waves (`MTG_BP_WAVES=1` default).** Wave phases run "while the node's
  budget allows; unlimited => exhaustive"; a budget-skip increments `g_fs_trunc_events`, demoting
  enclosing no-wins to unknown. Genuine for the two hosts that HAVE a wave phase
  (`FSLineWin` m1 ~:28109, `SolveWithLookahead` ~:30623).
* **EnumGroupCap — PARTIALLY REFUTED (fan-out finding).** The cap is applied at EVERY searched
  host, but only two hosts have wave phases. Four uncovered drop paths, none counted:
  (a) **`FSLineTail`'s second main** (~:26855) has no wave phase — its design doc concedes this
  with a frequency argument, exactly the reasoning the invariant rejects; (b) the **fresh-spend
  axis** is enabled for wave 0 only, falsifying "wave 0 plus tranches exactly partition the
  uncapped plan space"; (c) **condemn-suppression** guards wave 0 only — tranche plans are
  filtered by the very m2 condemnation the guard exists to keep off the root (live on AL/5C);
  (d) **`!beam_here` skips the whole wave phase** at beamed nodes, silently when the beam itself
  never cuts. Aggravating: an uncounted cap drop is laundered into a bound-qualified refutation
  by the no-win store (`g_fs_trunc_events == trunc_at_entry` gate).
* **Rollout/playout policy + value leaf.** The exempt layer; unchanged.

## 2. Sound at any budget (identity, not policy)

* **Canon continuation (`MTG_BP_CANON_CONT`, tight scope).** ACT-vs-PASS judged by greedy's own
  Solve at the same state; PASS falls through verbatim.
* **Colour-exact affordability (Hall's condition) — the strongest-argued gate in the engine.**
  Every approximation runs supply-up; unmodellable shapes (scaled/burst sources) stand the whole
  test down; karoo bundles exact; hybrid pips un-baked to two-colour masks; plus a 37k-rejection
  probe with zero false rejects. Caveat: coloured-pip reducers are exempted at the searched site
  but NOT at the leaf twin (playout-exempt, but the leaf misprices what the enumerator branches on).
* **Mana-total prune / selection-exact gate — identity BY ENUMERATED EXCEPTION.** Sound relative
  to `CanPay`, bails to INT_MAX on every unmodelled discount. The bail list is a maintenance
  contract, not a proof, and has been wrong twice (Rite-of-Flame triangular; the gi=22 Hinata
  credit). The class is live; any new cost-modifying mechanic must be added to the bail list.
* **Tap backtracker + flow-prune oracle.** Exhaustive DFS, no node/time cap; the oracle is a
  strict under-approximation that bails on anything unmodellable. Per-COST payment is
  ORDERING-ONLY by proof ("only picks WHICH legal payment — never whether one is found").
* **TT / no-win memos.** Full 128-bit key compare; the no-win store is bound-qualified AND
  truncation-watermarked. BUT see §4 for the three verify/key caveats the fan-out found.

## 3. Measured ZERO at ship settings (watch, don't fear)

* Masked breakpoint classes (`fell-to-greedy: class-masked 0` everywhere instrumented) and the
  empty-cands fallback (0 everywhere). Keep both counters in any future audit run.

## 4. THE WATCH-LIST — classified, by slice

### 4a. Condemnation + fcprune (all cites re-verified at the sharp edges)

* **THE TRUNC-DEMOTION GAP (VERIFIED — the sharpest systemic defect).** A condemnation drop
  bumps only `g_condemn_drops`; `CondemnFilterArmed` — which exists precisely because "a no-win
  from it is not a complete refutation" — is consulted ONLY by the default-OFF tranche
  (TurnSolver.cpp:27190). So on the two decks shipping the m2 filter ON (AntiLifegain,
  FiveColour), a filtered no-win passes the truncation-clean gate (:28314, :26264) and is cached
  as a sound refutation. **The same holds for every mana gate and every uncounted cap drop in
  this file — nothing outside budget/beam sites increments `g_fs_trunc_events`.** The predicate
  and counter both exist; wiring them is a one-line-class, GT-affecting fix (proposed, not made).
* **Main-phase order condemnation (ships ON: AL + 5C) — DROP-hard.** The sibling-coverage half
  is structurally sound (pre-combat enumeration never filtered; root set never suppressed); the
  per-deck value leg ("m1-cast ≥ m2-cast") is asserted + A/B'd, and has been falsified four
  times, each patched by a carve-out (free casts, joint-affordability, the re-arm pair — an
  explicit "NO m1 sibling covers the line" admission — and the Faeburrow class, where escalation
  to d7/32× "could not recover them… proving FILTERED, not underexplored" — the infinite-budget
  instrument already run once, and it found the class non-recoverable). The condemned tranche
  (the counterexample generator) is DEFAULT OFF. The escalation re-scores honestly but only a
  fixed top-K=3 of a systematically-biased ranking — K grows with nothing.
* **`MTG_CONDEMN_M1_BP` (default ON) — DROP-hard structurally, MEASURED INERT post-canon
  (2026-09-03).** Applies the m1-snapshot rule at breakpoint continuations with NONE of the bp
  filter's soundness gates; its old byte-identity evidence predated the canon-continuation
  adoption. Re-measured on the shipped binary (heurarm slot added, arm-fold fix in, 2000
  games/arm, seed 5500001): **digest-IDENTICAL on both AL and FiveColour.** The claim holds on
  the new baseline; entry closes clean, re-check again if canon scope ever widens.
* **fcprune — PROBED, KEEPS ITS SHIP (2026-09-03).** The dominance argument is conditioned on
  "a subset that casts both cards" and fails in theory when the cheap card is unpayable
  post-combat — but the probe (`MTG_FREECAST_PRUNE=0`, FiveColour 1000 games, seed 5500001):
  ZERO outcome moves, identical average. A ~-7%-searched-wall work-saver with no measured loss;
  under restoration constraint (1) it stays. The theoretical hole remains documented; re-probe
  if a deck ever leans on cheap free-casts post-combat.
* Breakpoint ("sound") condemnation is armed NOWHERE by default; drops measured 0/11.42M when
  sound. The still-reachable unsound arms are documented measured-worse; never ship them.
  Ranges (`CastOrderRankLatest`) are re-admit-only — recoverability-positive.

### 4b. Mana/affordability gates in the enumerator

* **`SubsetPayable` colour-presence gate — DROP-hard, INCONSISTENT with the flat gate
  (VERIFIED).** `have[]` is board+floating only, while the flat gate credits a same-subset mana
  rock's production by real colour (rock_mana, stamped "so the enumerator can fund the rest of
  the subset off it"). A subset whose missing colour comes from the rock it casts passes the
  flat gate and dies here; EF (the only rescue) is default OFF. Live cards: Izzet Signet
  (Hinata2), Ancient Cornucopia (FiveColour). **MEASURED (2026-09-03, `MTG_SUBSET_ROCK_COLOR`
  rescue-only lever, 1000 games/arm, seed 5500001): the hole FIRES on FiveColour — digest moved
  (the widened gate admits subsets that get CHOSEN into play) with ZERO games moved on outcome
  (identical win turn every game); Hinata digest-identical (the Signet never rescues at ship
  settings).** So the cut lines are reachable but not strictly better at this sample: an
  adoption candidate on gate-consistency grounds, quality-neutral, awaiting the USER.
* **Irencrag waste gate — PROBED AT 16x BUDGET, KEEPS ITS SHIP (2026-09-03).** The b320 probe
  (hinata, 300 games, gate on/off): gate-ON is BETTER even at 16x — off makes 3 games SLOWER
  (gi169 4→5, gi254 7→8, gi277 5→6; the wasted-subset exploration bleeding budget) and 1 faster
  (gi57, unwon→T8). **gi57 at d8 b0: T8 in BOTH arms — the b320 gate-on unwon was budget
  starvation, not a reachability cut. The gate FULLY PASSES the definitive test; no rescue
  needed; entry CLOSED.** The soundness is STRUCTURAL, not just measured (USER, 2026-09-03:
  *"The only way we could have that line is if we didn't re-evaluate after drawing"*): the only
  subset the gate deletes is the PRE-COMMITTED {cantrip, Irencrag} that locks the restrictor in
  before the draw is seen, and the {cantrip}-only sibling's breakpoint continuation re-enumerates
  on the post-draw hand, where {Irencrag, drawn payoff} passes the gate — so the deleted variant
  is redundant BY CONSTRUCTION wherever draw points trigger re-enumeration. GENERAL PATTERN: any
  gate deleting an act-before-information variant is sound exactly where the information point
  arms a re-solve (the ACQ_RESOLVE/TOP_RESOLVE family exists to guarantee this); where re-solve
  arming is MISSING (the recorded Vial-deployed-digger edge), such gates are NOT automatically
  safe. The in-code "deletes a real if rare line" comment predates this analysis and overstates.
  The follow-on USER discussion (spanning tails = reservation hypotheses; "{Ponder, Irencrag} vs
  {Ponder} are the same up to the breakpoint except perhaps in how they tap mana") is formalized
  as the deferred design `breakpoint-truncation-via-reservation-axis.md`.
  Also: `max_casts_after` counts followers by `CastOrderRank`, so a subset legal in a different
  order is dropped before the (post-enumeration) ordering search can see it.
* **Strive-K mana ceiling + `ActivateRevealTop` kmax=3 — HATCHES BUILT + PROBED (2026-09-03).**
  `MTG_STRIVE_CEILING` (default ON; =0 removes the +2-fudge break) and `MTG_REVEALTOP_KMAX`
  (int, default 3; 0 = pool-bounded). Revealtop probed on stompy (the ONLY live deck —
  Call of the Wild), 1000 games value-play uncapped vs capped: **BYTE-IDENTICAL digest, the cap
  never binds at ship settings — measured inert, closed.** The strive ceiling has NO live deck
  (Twinflame exists only in the archived Mirrorwing v1 list); the hatch waits for one.
* **`MTG_EMIT_PRUNE` (default OFF)**: per-card dimension drop resting on an enumerated
  `budget_can_grow` list that has shipped lossy twice. Leave off; gate before reconsidering.
* **Cross-cast greedy stranding — INSTRUMENTED 2026-09-03; the original claim was
  OVERSTATED.** Per-cost payment is complete, but `BatchPrepayMainCasts` declines on ~11
  conditions, after which each cast pays alone and cast #1's tap can strand cast #2, which is
  then silently dropped at apply (:17645). **CORRECTION: the plan is NOT "scored as if it
  resolved"** — the rollout's ApplyPlanDirect drops the same cast through the same
  TapForCostDirect (lockstep by construction), so the plan's score reflects the drop. The real
  question is narrower: is the JOINT-payment line (both casts resolve) reachable by any
  enumerated route when sequential greedy strands it? `MTG_AFFORD_AUDIT=1` at value-play
  settings, 300 games/deck, all 16 suite decks: real (executor) drops fire on 11 decks —
  dragonstorm 304 (16% of real cast attempts, 210 stranded accelerants), hinata 141, dragons 83,
  th 65, stompy 58, slivers 43; zero on antilife/auras/knights/creature_giving/mirrorwing.
  Level-2 board dumps split the drops into TWO classes, NEITHER of which is order-dependent
  stranding: **(A) restricted-source optimism** — single-cast turns priced against mana the
  spell cannot legally use (dragons: T1 Lightning Bolt vs Haven of the Spirit Dragon's
  dragon-only "wild" or Sol Ring's {C}{C}, colour-short ×56; kitty's white one-drops, stompy's
  dorks) — enumeration optimism per the affordability arc's LAW, filtered honestly by the
  rollout; **(B) ritual-chain optimism** — one-land turns declaring whole chains priced with
  `MTG_RITUAL_SEQ_CREDIT=1` float credit (dragonstorm t1: Medallion {2} + Desperate Ritual +
  Irencrag Feat all dropped on a lone Mountain) — the "plan is a PROPOSAL, apply trims"
  mechanism working as designed.
  **REACHABILITY A/B VERDICT (2026-09-03, 1000 games × 16 decks/arm at value-play, both worlds
  through the shared prepay; every mover replayed per-lever and at d8 b0):** 8 movers total in
  16,000 games. **THREE CONFIRMED VIOLATIONS, ALL FIXED BY `MTG_PREPAY_MIXED` ALONE** — dragons
  gi460 + gi776 (default d8b0 = T6, mixed d8b0 = T5) and slivers gi777 (default d8b0 = T5, mixed
  d8b0 = T4). All three are the RESTRICTED-SOURCE batch conservatism MIXED was built for (a
  mixed creature/noncreature batch reads Haven of the Spirit Dragon / Sliver Hive as {C}-only →
  combined solve fails → decline → the per-cast greedy strands a cast the two-stage solve pays).
  MIXED's target population is exactly the `combined UNPAYABLE` decline class
  (`MTG_PREPAY_PROBE`: 1,172/1.5M prepay calls on dragonstorm, 315 on hinata, 420 recorded on
  slivers) — small and surgical, and MIXED caused ZERO regressions in the sample.
  **`MTG_PREPAY_PRODUCER`: closed OFF with a sharper record than 2026-08-18** — its one
  ship-settings rescue (fivecolour gi590 T6→T5) RECOVERS at d8 b0 under default (budget churn,
  not a violation) while it alone causes all four regressions (stompy gi292/gi515/gi526,
  fivecolour gi70). Dragonstorm — the drop-heaviest deck (304 real drops/300 games) — is
  byte-IDENTICAL across arms: its ritual-chain drops are pure priced-optimism trimming.
  What invalidates the 2026-08-18 "metric-inert" parking for MIXED: the question changed (d8 b0
  unreachability, not aggregate), and per-game d8b0 attribution now shows 3 non-clairvoyant
  faster lines truly unreachable without it. hinata gi1197 remains strand-SELECTION (cast-order
  family), not allocation. The USER's mana-allocation exemption remains safe per-cost, NOT
  across casts. MIXED-only full-sample confirmation + adoption chain: see the adoption record.

### 4c. Cast order + discard

* **The ordering dimension is collapsed (all decks but Dragonstorm): one order per subset, ever.**
  Membership is untouched, but the apply silently no-ops an unpayable cast — a wrong order loses
  a cast the search scored as resolving. The `MTG_SEARCH_ORDER` hatch itself silently reverts to
  canonical at k ≥ 6 (`k! > 120`, no counter). Dragonstorm's targeted generator always includes
  identity, with a self-documented Medallion-stagger hole. Ranges RESTORE reachability that a
  promotion cost; they never widen beyond baseline, and the ladder stands down on order-dependent
  costs — precisely where order matters most. This is the recorded cast-order-prune question,
  structurally confirmed; the voided stompy verdict concerned `MTG_STOMPY_ORDER` (also off), and
  the gate-2 re-derivation is still un-run.
* **Main-phase filter's soundness claim is FALSE AS WRITTEN** ("combat can only ADD options…
  never remove them"): attacking taps non-vigilant mana creatures, so deferring to m2 can make a
  cast unaffordable with no re-open. The engine patches this per-deck (vigilant-scaler carve-out,
  `AvailableManaPoolNoAttackers`) — an admission the general claim fails. Live on AL + 5C.
* **Cleanup discard — CORRECTED (2026-09-03): the searched pass is ADOPTED and default ON
  (`MTG_SEARCHED_DISCARD` + `MTG_DISCARD_RELINE`, 2026-07-31), so the decision-point shed IS
  searched at searched depths.** The initial fan-out read ("pruned to one, prefix-only") described
  the heuristic consumer sites without the adopted searched pass on top; the three unwon-at-d8/20s
  combo games were the PRE-adoption evidence, and the adoption record closes them
  (monotone-better, 39 faster / 0 slower held-out). What remains heuristic, per that doc's own
  list: (a) sheds 2..N searched by coordinate descent with the heuristic assumed for the tail;
  (b) rollout-INTERNAL discards stay heuristic (playout-exempt per the scope ruling — and the
  USER notes the definitive d8 b0 test needs no rollouts, so this residue does not block the
  proof); (c) `discard_protect` now serves as prune/tie-break, tuned pre-adoption, due a
  re-measurement. If a deck still misplays a discard, the USER's hypothesis is the per-deck
  BUCKETING (ranking rule) is mis-defined for that deck — a data fix, not a mechanism gap.
  **THE BUCKETING HYPOTHESIS SCREENED FLEET-WIDE (2026-09-03): NO MIS-BUCKETING FOUND.**
  `MTG_SHED_STATS` ranked the generic-rule decks by rollout-shed traffic (mirrorwing 40,475/200
  games — 4x anyone, and its prior screen predated the 2026-08-24 decklist swap; dragonstorm
  10,307; auras 4,578; knights 2,813; burn 2,589; CG 2,580; slivers 2,033; goblins 531), then
  `analyze_deck.py --discard-analysis` re-ran on all of them + hinata under today's engine:
  hinata, mirrorwing (shipping list), burn, goblins, auras, dragonstorm = **STATUS_QUO_OK** (the
  shipped ranking is at/near the searched optimum); CG, knights, slivers = **DISCARD_INERT** (no
  real-play shed reached). Combined with the shipped bucket policies
  (AL/5C/kitty/stompy/dragons/minotaur + TH's own ranking), every suite deck's cleanup ranking
  is either authored-and-validated or measured at the searched optimum.
  Also: same-named copies are NOT interchangeable for `graveyard_replace_shuffle_library`
  cards — a future name-keyed bucket would be unsound.

### 4d. Dedupe keys + caps + karoo (the incomplete-identity class)

* **Land-signature dedupe — FIXED + ADOPTED (2026-09-03, the audit's FIRST confirmed
  violation).** `MTG_LAND_SIG_COMPLETE` now DEFAULT ON with the 17-param extension and the
  ccoa dominance promotion; 4 Creature Giving games proved d8b0-unreachable T4s under the
  legacy signature (City of Brass / Forbidden Orchard collision). All three GT tiers
  rebaselined; the split-vs-promotion lesson (behavioural COSTS = signature splits,
  strictly-optional RICHES = `land_bonus` promotions) recorded at the read site. Residue:
  name-keying not measured separately. Doc/code mismatch still open: `MTG_FORCED_EARLY_LAND`
  is commented "default on" but reads default OFF, while other docs describe it as adopted.
* **`plan_signature` — IDENTITY-INCOMPLETE in autonomous search, X-axis MEASURED INERT
  (2026-09-03).** `chosen_x`, `enchant_target`, `splice_count`, strive-K are keyed only under
  `HumanPlayActive()`. The X-collapse concern was tested with a dedup-site audit
  (`MTG_SIG_X_AUDIT`, counts dropped plans whose kept twin differs only in chosen_x, both
  directions): **zero collisions in 200 Mirrorwing games** — the only deck whose provider
  proposes two X values — and every other provider proposes a single X, making collisions
  structurally impossible today. The instrument stays in-tree; re-run it if a provider ever
  proposes multiple X values or a new X card lands. The `MTG_UNPRUNED(XSpell/TrickTarget)`
  oracle-arm concern (opened ranges collide and are discarded) remains a code-reading claim —
  check the audit counter whenever those oracles run. Rescued axes:
  tutor/scry/dig/lackey/ponder/discard/sac-land/fresh-spend.
* **Karoo timing/target — DROP-hard forced policy.** `Plan` has no land-timing field; enumerator,
  rollout and executor all read `if (etb_bounce_land) defer` off the card; `MTG_NO_KAROO_DEFER`
  swaps one forced policy for the other — no configuration makes both branches reachable in one
  search. Bounce target = `BounceLandCandidates().front()` only (the identical class
  `MTG_SAC_AXIS` fixed for sacrifices). The KEY (`karoo_deferred`) is fine; the loss is upstream.
  The self-bounce prune's dominance comment is unsound if a Karoo ever pairs with landfall
  (latent today). SEPARATE LIVE BUG surfaced: the rollout suppresses the continuation land while
  the drop is reserved (:17431) but the executor twin (AIEngine.cpp:2974) has no such guard —
  executor can consume the drop and never play the Karoo the rollout scored; `PlanSig` excludes
  the land field so `MTG_CONT_DIFF` cannot see it. Needs its own fix + measurement.
* **Cache-key caveats.** (a) Enum memo CROSS-HOST COLLISION: `EnumeratePlansWithLand` and
  `EnumeratePlansM2Memoized` share one `enummemo::t_cache` namespace while their bodies differ
  (breakpoint variants appended vs not); both reachable in one epoch on KittyEquipment. Fix
  shape exists in-repo (`SearchedSecondMainMemoized` folds 0x5E2C). (b) The VERIFY harnesses
  compare via `solvememo::SamePlan`, which is blind to every searched-axis pin (`scry_choice`,
  `bp_choice`, `sac_pins`, …) — the clean 813k/604k records are statements about cast content,
  not about which branch was returned. The canon verdict memo, FSLineCache, bp-enum cache and
  `g_mana_cache` have no harness at all. (c) `g_mana_cache` key omits `g_tap_keep_last_card` /
  `g_scripted_tapmode` (narrow, same class). (d) The batch-arm leak in (b)'s two unlisted caches
  is FIXED (§0).

### 4e. Second main, attack projections, node scope

* **`DeckUsesSecondMain` whitelist — CLOSED 2026-09-03, the incompleteness was REAL and is
  fixed (§6.7: the Utvara+ping pairwise rule; dragons net −50/1000 games).** Original entry:
  Dragons/Dragonstorm (Utvara Hellkite) and Knights (Adeline) read
  `uses_second_main=false`, skipping the entire post-combat main at any budget. Whether that
  costs real value is deck-specific (attack-created tokens are summoning-sick) — NEEDS-JUDGMENT,
  fix is per-param. The in-code activating comment names untap-on-attack (Bear Umbra) as a case;
  no such param is in the predicate.
* **Attack-projection-gated prunes — the exemption LEAKS.** Three default-ON prunes delete CAST
  subsets on a heuristic lethal projection's `!wins`: Irencrag-waste, the sac-land hold
  (burn opts in), the ritual-payoff guard. A projection under-count is budget-permanent. The
  engine ships the tripwire: `MTG_SV_LETHAL_AUDIT` counts plans dropped that were actually
  lethal (`g_pruned_lethal`). **RUN (2026-09-03, burn — the sole SV_HOLD opt-in — 1000 games,
  seed 5500001): strict-pruned 788,651, ACTUALLY LETHAL 0, exception-rescued 29,198 / lethal 0.
  The projection under-count does not fire on burn; entry downgraded to watch.** Sharper and
  still open: the searched enumerator's `wins`
  deliberately omits `ExtraLethalDamage` that the greedy twin includes — every lethal-exempt
  drop in `eval_and_push` has a strictly narrower exemption than its leaf twin.
* **`MTG_BP_NODE_ROOTTURN` (adopted ON) + the unconditional site-3 wave-drop**: on every
  non-root ply, a site-3 breakpoint has no variants, no wave slot, no node — one greedy
  continuation, n−1 unreachable at any budget. Doctrinally sanctioned by the USER's 2026-09-02
  scope ruling (the RECORDED path is re-decided at each turn's root), but against this charter
  it is a lossy evaluator element: the error is re-incurred at every ply at every budget, and
  "fixing only the root fixes nothing" is the in-code measured finding. An infinite-budget arm
  MUST set `MTG_BP_NODE_ROOTTURN=0 MTG_BP_NODE_KEEPWAVE3=1` or it silently measures the
  narrowed structure. Node children resume with `bp_capture=nullptr` (nesting = canon
  `cands[0]`; the L×W-not-W^L trade, acknowledged in-code). Latent hardening: the node pend
  gate keys on capture-pointer presence, not a host flag.
* **"Zero executor greedy" is a statement about the REPLAY mechanism** (`execgreedy` counts the
  executor's own re-solves, which committed lines bypass via `replay_recorded`), not that no
  greedy decided the committed line. Recording applies without a capture are greedy-decided and
  replayed. Mitigations: measured — the RECROOT arm (canon at root-turn recording applies) was
  literally outcome-identical to tight, so this is measured-inert at root turns.
* **The interior second main is greedy at all depths for every non-opted-in deck**, and the
  in-code record states the global searched-m2 lever measured worse "and does not recover with
  budget" — the one place the engine already measured non-convergence directly. The three
  opted-in decks recover with budget (later passes overwrite pass 0). `SecondMainUnproductive`
  is inert, and unsound as written if ever adopted (spectacle grows neither hand nor board).
* **The escalation beam (`MTG_ESC_BEAM`, per-deck `value_play.beam_width`) — shipped-ON width
  cap, ACCEPTABLE UNDER THE REFINED BAR, watch-listed.** Live on Anti-Lifegain / Dragonstorm /
  Hinata2 (value W3 ld2 at d5; static W20 ld1 at d3 for any value_play deck). The value probe
  runs unbeamed, `leafdepth` protects the root's candidate set (only deep score-estimation
  frontier is pruned), truncations are counted, and d6+/`MTG_ESC_BEAM=0` disarm it — so it
  violates only if the estimate coarsening flips the argmax to a genuinely worse play, which its
  adoption A/B (quality-neutral-or-better, 0 worse seeds) measured against. NOT
  budget-recoverable at fixed depth; adopted 2026-07-18, predating the no-lossy-truncation bar.
  (The `MTG_ESCALATION_GATE` learned skip next to it is env-only, default OFF, dormant.)
* **Board-lethal short-circuit**: sound under this metric (re-verifies through the enumerator,
  falls through if unconfirmed). Hidden premise = passive goldfish opponent; becomes a hard
  prune the day a real opponent exists.

## 5. The verification instrument (updated)

**USER protocol ruling (2026-09-03): "unreachable means TRULY unreachable, which only can be
proved with unlimited budget… d8 b0 (unlimited, max depth) is the definitive test, but may be
slow depending on the case."** So the two-stage form: a cheap ship-settings A/B finds CANDIDATE
moved games; then replay just those games (`--game-index`) at **`--depth 8 --budget-ms 0`** in
both arms — the definitive per-game proof, affordable because it runs only on the candidates.
(It needs no rollouts — at max depth the search carries the line itself — so the
rollout-internal heuristic residue does not contaminate the proof.) Any game the lever-off arm
wins earlier at d8 b0 — after a clairvoyance check — is a lossy element cutting a genuinely
better line. `g_fs_trunc_events == 0` in the unlimited arms is
the precondition — **meaningful on AL/5C since the 2026-09-03 trunc-demotion wiring (§6.1;
before it, condemnation drops were invisible to the counter). The same session opened the
escalation's K to the whole pool at unlimited budget (§6.6), so an unlimited arm's escalation
converges too.** Arm-3 construction notes:
* Flags an unlimited arm must set to open the searched structure fully:
  `MTG_BP_NODE_ROOTTURN=0 MTG_BP_NODE_KEEPWAVE3=1 MTG_BP_CANON_REC=1 MTG_SEARCH_SECOND_MAIN=1
  MTG_M2_D0_SEARCHED=1 MTG_IRENCRAG_WASTE=0 MTG_SV_HOLD=0 MTG_NO_RITUAL_PAYOFF_GUARD=1
  MTG_NO_LETHAL_CUT=1 MTG_UNPRUNED=1 MTG_ESC_BEAM=0 MTG_CONDEMN_TRANCHE=1
  MTG_LAND_SIG_COMPLETE=1` (+ per-suspect flags).
* **Three entries have NO hatch and cannot be A/B'd at all**: `SubsetPayable`'s colour-presence
  gate (lever now built), the strive-K ceiling, the `ActivateRevealTop` cap.
* Instruments that make a run legible: `MTG_SV_LETHAL_AUDIT=1`, `MTG_M2_YIELD_STATS=1`,
  `MTG_CONDEMN_TRANCHE=1 MTG_TRANCHE_TRACE=1` (the rescue counter IS the counterexample
  generator), `MTG_COLOR_EXACT_PROBE`.
* Priority queue (cheapest × sharpest): (1) ~~`MTG_SUBSET_ROCK_COLOR` fires-check~~ DONE —
  fires on FiveColour, outcome-neutral (see 4b); (2) ~~`MTG_CONDEMN_M1_BP` A/B~~ DONE —
  digest-identical both decks (see 4a); (3) Irencrag three-arm reachability probe; (4) fcprune
  third arm on FiveColour; (5) `g_pruned_lethal` audit run per deck; (6) hatches for
  strive-K/revealtop, then their probes; (7) the tranche probe re-run on AL/5C (the standing
  in-code instruction, promoted here to a required gate whenever condemnation wires into a new
  provider).

## 5b. BUILT from the audit: refuted-follow (`MTG_REFUTED_FOLLOW`, default OFF pending adoption)

USER direction (2026-09-03): *"Once we have searched fully to the end of max_turn we should
stop… choosing a line that seems the best and following it out"* — and *"a way to bound deep
searches."* The gap it closes: the FSLine no-win cache stops re-search WITHIN a decision, but
both memo tables are per-call, so a doomed game re-proved its doom with a full search EVERY turn
(the five-hour TH game: 138k leaf lookups, 100% no-win). Mechanism: when a top-level search
finds no win with the committed pass covering every remaining turn
(`turn + searched_depth − 1 >= max_turns`, so leaves are terminal states, not estimates) and a
ZERO `TruncEvents()` delta across the decision, the game is marked refuted: the WHOLE best-graded
lost line is committed and followed out; phases the line stops covering (post-reline) take the
greedy plan; rollouts are untouched. Later turns' searches would explore a subset of the refuted
space, so this is outcome-identical by construction. **MEASURED (2026-09-03, th 1000 + hinata
500 + mirrorwing 500, seed 5500001): 0 outcome moves, identical averages; indicative wall
(contended) hinata −19% / th −33% / mirrorwing −46%.** Unwon-game digests move (a followed line
differs in actions), so adoption = default flip + suite + GT rebaseline + quiet-box wall probe.

## 6. Proposed fixes surfaced by this audit — BUILT 2026-09-03 (second session; measurement chain
below). Hatches: `MTG_TRUNC_COMPLETE=0` (counting), `MTG_GW_PARITY=0` (tranche parity),
`MTG_ENUM_MEMO_HOSTTAG=0` (memo identity), `MTG_KAROO_BP_LOCKSTEP=0` — each default ON, each =0
arm verified byte-identical at smoke scale (48/48, 0 configs changed).

1. **DONE — trunc-demotion wiring.** SEARCHED-space condemnation drops (both the m2 filter and
   the breakpoint twin) now bump `g_fs_trunc_events`; rollout/executor drops exempt per the
   scope ruling. THE MEMO SUBTLETY THE PROPOSAL MISSED: a memo hit replays neither the drop nor
   the bump, silently re-opening the gap — so `enummemo::Entry` and the searched-m2
   `solvememo::Entry` now record their body's (condemn_drops, trunc_delta) and REPLAY both on
   every hit (exact parity with unmemoized counting; also fixes the escalation detector's
   blindness to memoized filter-touched subtrees).
2. **DONE — the four group-wave holes.** (a) `FSLineTail`'s m2 enumeration counts its group-cap
   drop as a truncation (no wave phase re-opens it there); (d) the beam's wave-phase skip at
   `FSLineWin` counts; (b) FIXED not counted — `FSLineWin`'s tranche re-enumeration now opens
   the fresh-spend axis like its wave 0 (scorer mirrors the win-realization admissibility
   check); (c) FIXED not counted — the lookahead enforcing-root's tranches now run under the
   same `CondemnSuppressGuard` as its wave 0. Interior condemn-filtered tranche plans are
   covered by fix 1's counting.
3. DONE previously (land-sig adoption, see 4d).
4. **DONE — memo identity, three instances.** The m2-plain host folds `0x371D`; PLUS two
   instances the audit had not named: `g_fresh_axis_enum` folded by the m1 host (FSLineWin's
   fresh-emitting enumeration must not share entries with lookahead hosts — a cross-host hit
   either starved FSLineWin of fresh variants or leaked unvalidatable ones into a rollout), and
   `g_condemn_suppress` folded by both m2-side caches (the escalation's honest re-run guards its
   TT with `esc_tt` but hit these two thread_local caches' FILTERED entries — the honest re-run
   was cache-contaminated on AL/5C).
5. **DONE — `SamePlan` compares all plan-level axis pins** (scry/rad/etbdig/tutor/sac_pins/
   tapmode/freshmode/lackey/ponder/discard/vial/dig/bp_choice/bp_at/bp_wave0/atk_dork_release).
   Verify-only callers; prior clean records remain statements about cast content.
6. **DONE — escalation K opens to the whole pool at unlimited budget** (budgeted play keeps
   K=3, byte-identical at ship settings; the d8b0 instrument now converges).
7. **DONE — the whitelist WAS costing real wins; per-param rule shipped.** The
   `MTG_FORCE_USES_M2=1` probe (1000 games/arm at value-play): **dragons 51 faster / 1 slower
   (net −50 turns, avg 5.635 → 5.585), dragonstorm 14 / 4 (net −10), knights 2 movers net 0.**
   Mechanism: Utvara Hellkite's attack-created Dragon tokens raise the Dragon count that
   Scourge of Valkas / Dragon Tempest `dragon_ping_on_enter` ETB damage scales on, so a
   post-combat dragon cast pings strictly harder — a combat-generated resource (2c-bis) the
   whitelist read as absent, and a skipped phase is not budget-recoverable at ANY setting (the
   whitelist's confirmed violation of the invariant). Shipped: pairwise rule
   `attack_per_matching_creates_tokens > 0` AND any `dragon_ping_on_enter` card
   (Skyhunter/Puresteel form; leaves Adeline-only knights correctly off — measured net 0).
   Rule-arm reproduces the probe arm BYTE-FOR-BYTE on dragons+dragonstorm; knights identical to
   default. **All 5 rule-ON ship-settings regressions (ds gi157/306/384/749, dragons gi47)
   CONVERGE at d8 b0 — identical win turn both arms — so each is budget churn, not a cut.**
   Hatch: `MTG_NO_UTVARA_M2=1` restores the pre-rule whitelist. Cost: m2 search now runs on two
   more decks (wall indicative-only, box contended; dragons pooled job ms roughly +77%).
8. **DONE — Karoo lockstep fix.** The executor's searched-continuation land play now honours
   `karoo_deferred` exactly as the rollout's `bp_play_searched_land` does
   (`MTG_KAROO_BP_LOCKSTEP=0` restores).

Also closed: the `MTG_FORCED_EARLY_LAND` doc/code mismatch (4d residue) — the hook is
WITHDRAWN per its provider header (disjoint-seed re-measure was +1.87% rollout calls, worse);
default OFF is deliberate, the stale TurnSolver "(default on)" comment is fixed.

**MEASUREMENT (2026-09-03, all contention-proof — the USER flagged the box as contended, so no
wall verdicts):** 16 decks × 1000 games/arm at value-play on the pre-fold binary: **ZERO
outcome movers in 16,000 games; exactly 2 play-digest movers, both FiveColour, both UNWON
games** (gi630/gi636 — the demotion changing search inside doomed games only). Final-binary
re-run of both arms + the force-m2 probe: in flight at write time. Smoke: flag-off arm AND
defaults both 48/48 byte-identical (the counting fixes change no smoke-scale committed play).

## Context

`docs/design/bp-greedy-continuation-deletion.md` (the adoption + rulings), `no-lossy-truncation`
USER bar (2026-08-14), `heuristics-wired-as-prunes` doctrine, `docs/design/group-waves.md`,
`docs/design/post-breakpoint-search.md`, `docs/design/searched-cleanup-discard.md`,
`docs/design/batch-pool-contamination.md`, `docs/design/exemption-free-condemnation-order.md`,
`in-tree-greedy-reachability-hole` (the measured pre-node unreachability the 2026-09-02 arc
closed), `docs/design/escalation-beam-verify.md`.
