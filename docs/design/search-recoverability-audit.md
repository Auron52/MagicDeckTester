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
* **Strive-K mana ceiling — DROP-hard, NO HATCH** (the `+2` fudge credits no ritual/rock/reducer;
  the break sits outside `UnprunedGate::TrickTarget`). **`ActivateRevealTop` `kmax=3` — hard
  width cap, NO HATCH.** Both need a hatch before they can even be A/B'd — itself the finding.
* **`MTG_EMIT_PRUNE` (default OFF)**: per-card dimension drop resting on an enumerated
  `budget_can_grow` list that has shipped lossy twice. Leave off; gate before reconsidering.
* **Cross-cast greedy stranding — DROP-hard.** Per-cost payment is complete, but
  `BatchPrepayMainCasts` declines on ~11 conditions, after which each cast pays alone and cast
  #1's tap can strand cast #2 — which is then SILENTLY DROPPED at apply (:17645) while the plan
  was scored as if it resolved. The USER's mana-allocation exemption is safe per-cost, NOT
  across casts. Fix shape: shrink the decline set or joint re-solve on apply failure.

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
  Also: same-named copies are NOT interchangeable for `graveyard_replace_shuffle_library`
  cards — a future name-keyed bucket would be unsound.

### 4d. Dedupe keys + caps + karoo (the incomplete-identity class)

* **Land-signature dedupe — IDENTITY-INCOMPLETE, LIVE.** `MTG_LAND_SIG_COMPLETE` ships OFF; the
  legacy signature has **6 live collisions across 5 suite decks** (re-run of the doc's audit:
  Creature Giving, Dragonstorm, EldraziDisplacer, Knights, Minotaur, slivers_vial), and **17
  land params are invisible to BOTH signatures** — Mariposa Military Base vs Shivan Gorge
  (whose damage ability is "this deck's same-turn kill") cannot be separated by ANY existing
  configuration; which is enumerable is decided by draw order. No counter, no reopen, no
  `MTG_UNPRUNED` gate. Recommended order: add the 17 params (byte-identical for decks lacking
  them), then measure name-keying separately. Doc/code mismatch: `MTG_FORCED_EARLY_LAND` is
  commented "default on" but reads default OFF, while other docs describe it as adopted.
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

* **`DeckUsesSecondMain` whitelist — DROP-hard but DATA-recoverable, and the whitelist is
  incomplete.** Dragons/Dragonstorm (Utvara Hellkite) and Knights (Adeline) read
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
the precondition — **which today cannot hold meaningfully on AL/5C** (condemnation drops are
invisible to it; see the trunc-demotion gap). Arm-3 construction notes:
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

## 6. Proposed fixes surfaced by this audit (NOT made — each is GT-affecting or user-owned)

1. Wire `CondemnFilterArmed` → `g_fs_trunc_events` (or gate the no-win stores on it): one-line
   class, restores the anytime contract on AL/5C.
2. Count the four group-wave holes (§1) or give `FSLineTail` a wave phase.
3. Add the 17 missing land params to BOTH land signatures; re-run the collision audit as a
   required step when any land param is added (it had not been re-run; 2 new collisions since).
4. Namespace-tag the enum memo's two hosts (0x5E2C precedent).
5. Extend `solvememo::SamePlan` to compare the axis pins, so the VERIFY harnesses verify what
   they claim.
6. `MTG_CONDEMN_ESC_K` → budget-scaled (or `|pool|` when unlimited).
7. Second-main whitelist: decide the Utvara/Adeline/untap-on-attack params (needs judgment on
   whether the skipped m2 ever has value for those decks).
8. The executor/rollout Karoo reserved-drop divergence (AIEngine.cpp:2974) — a plain lockstep
   bug, needs its own fix + measurement, filed separately from this audit.

## Context

`docs/design/bp-greedy-continuation-deletion.md` (the adoption + rulings), `no-lossy-truncation`
USER bar (2026-08-14), `heuristics-wired-as-prunes` doctrine, `docs/design/group-waves.md`,
`docs/design/post-breakpoint-search.md`, `docs/design/searched-cleanup-discard.md`,
`docs/design/batch-pool-contamination.md`, `docs/design/exemption-free-condemnation-order.md`,
`in-tree-greedy-reachability-hole` (the measured pre-node unreachability the 2026-09-02 arc
closed), `docs/design/escalation-beam-verify.md`.
