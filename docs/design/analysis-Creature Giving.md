# Analysis ledger — Creature Giving

In-flight state for the analyze-deck workflow on `decks/Creature Giving/Creature Giving.cod`
(60 cards). Updated continuously; readable by any resumed session or second machine.

## Deck concept

Gift the opponent creatures (Forbidden Orchard spirits, Hunted Phantasm's 5 Goblins,
Varchild's War-Riders cumulative-upkeep Survivors), then punish them:

- **Suture Priest** — each opponent-creature enter drains 1
- **Massacre Wurm** — ETB -2/-2 sweep (kills the 1/1 gifts) + each opponent-creature death drains 2
- **Defense of the Heart** — upkeep, opp ≥ 3 creatures → sac, fetch 2 creatures onto battlefield
  (the nut line: put Hunted Phantasm + Massacre Wurm with a Priest out ⇒ 5×enter-drain + all
  opponent tokens die × 2 × Wurms)
- Soul/Essence Warden gain us life (only matters for pain/shock/fetch life costs)
- Tutor shell: Enlightened Tutor (→ Defense of the Heart / Tree of Tales), Sylvan Scrying
  (land → hand), Crop Rotation (sac a land → land onto battlefield), fetches

## Stage 1 — coverage (2026-08-05)

14 missing: Hunted Phantasm, Suture Priest, Massacre Wurm, Soul Warden, Essence Warden,
City of Brass, Defense of the Heart, Sylvan Scrying, Breeding Pool, Crop Rotation,
Varchild's War-Riders, Azorius Chancery, Tree of Tales, Misty Rainforest.
Already covered: Forbidden Orchard (incl. the opp-Spirit model), Reflecting Pool,
Windswept Heath, Temple Garden, Enlightened Tutor, Stomping Ground, Forest,
Birds of Paradise, Overgrown Tomb.

All oracle texts + costs fetched live from Scryfall 2026-08-05 (this run's 2a).

## Card plan (tier, params, wiring)

| Card | Tier | Model |
|---|---|---|
| Breeding Pool | 1 | shock: `produces [G,U]`, `etb_pay_life_to_untap 2` |
| Misty Rainforest | 1 | fetch: `fetch_land_types [Forest,Island]` |
| Azorius Chancery | 1 | Karoo: `produces [W,U] ×2`, `enters_tapped`, `etb_bounce_land` |
| Tree of Tales | 1 | G artifact land (types [Artifact, Land]) — an Enlightened Tutor target |
| City of Brass | 1 | `produces WUBRG`, `tap_self_damage 1` |
| Soul Warden / Essence Warden | 2 | new `any_creature_enters_lifegain 1` |
| Suture Priest | 2 | new `own_creature_enters_lifegain 1` + `opp_creature_enters_life_loss 1` |
| Hunted Phantasm | 2 | new `etb_opp_creates_tokens 5` (+ etb_created_token_* 1/1 Goblin); unblockable inert (opp never blocks) |
| Massacre Wurm | 2/3 | new `etb_opp_creatures_debuff 2` (−2/−2-until-EOT collapse: kill opp creatures with toughness−damage ≤ 2) + `opp_dies_life_loss 2` |
| Varchild's War-Riders | 3 | new `cumulative_upkeep_opp_token` + upkeep_token_* (1/1 Survivor); Permanent.age_counters; ALWAYS pays (goldfish-dominant); trample/rampage inert |
| Sylvan Scrying | 1 | `tutor_to_hand`, `tutor_types [Land]`, `tutor_shuffle_after` (searched axis) |
| Crop Rotation | 2 | `sacrifice_land` (existing additional-cost path) + new `tutor_land_to_battlefield` (searched axis; fetched Orchard spawns its Spirit on entry, mirroring LandPlay's on-play hook) |
| Defense of the Heart | 3 | new `upkeep_sac_tutor_creatures 2` + `upkeep_sac_tutor_opp_min 3`; provider `SacTutorPutList` default = closed-form burst maximization (token-makers enter before sweepers); human chooser wired (bucket B) |

## Engine wiring (lockstep executor + rollout)

- `FireCreatureEnterWatchers` — called from inside the universal enter cascade
  (`FireEtbWatchers` top, gated on the entered permanent being a creature) so every existing
  enter site (cast, token, puts, off-suspend) fires it; PLUS explicit calls at the two
  opponent-spawn materialisation sites (GameEngine turn-start, TurnSolver turn-start).
  Gated on watcher params → byte-identical for every other deck.
- `FireOppCreatureDies` — opp-death drain; called from the Massacre Wurm sweep and the
  etb_damage_each_opponent inline prune.
- Upkeep additions (GameEngine::RestOfUpkeep + TurnSolver::SimulateEndAndStartNextTurn, same
  order both worlds): War-Riders age+tokens FIRST, then Defense of the Heart check
  (controller-optimal trigger order: the new Survivors count toward DotH's ≥3).
- Not wired (inert for this deck, noted): Vial-deploy enter sites do not fire the
  enter-watcher cascade (this deck has no Vial; pre-existing scope of the Dragon/Goblin
  cascades is identical).

## Known modelling collapses (for 6a disclosure; bracket-noted in cards.json)

- Massacre Wurm −2/−2 is now applied FOR REAL as an until-EOT `temp_tough_bonus` on the
  creatures present at each sweep resolution (fix `2026-08-06`, replacing the earlier
  "toughness−damage ≤ 2" collapse): two sweeps in one turn STACK (the second trigger of a
  simultaneous double-Wurm put kills at cumulative −4/−4 — profile spawns include 3/3s and
  4/4s, so this matters), and tokens gifted between sweeps only see later ones
  (CR 611.2c set-locking). Power reduction on survivors is still inert (opp never
  attacks/blocks). Remaining strict gap: −X/−X kills an indestructible creature only via
  toughness ≤ 0; no spawn schedule produces one.
- Varchild's War-Riders cumulative upkeep is ALWAYS paid (never sacrificed): paying is
  weakly dominant vs a passive opponent (tokens only feed our drains/DotH; the 3/4 body is
  kept). Pay-vs-sac not surfaced as a choice; disclosed.
- Suture Priest / Wardens "you may" triggers: always taken (strictly beneficial).
- Hunted Phantasm "can't be blocked", War-Riders trample/rampage: inert (opp never blocks).
- Forbidden Orchard model is the pre-existing per-turn spawn (see its bracket note).

## Progress

- [x] Stage 1 coverage
- [x] Stage 2a Scryfall fetch (all 14)
- [x] Stage 2 infra survey
- [x] Stage 2 implementation (params + watchers + upkeep triggers + land tutors; builds clean)
- [x] Stage 2c-ter viewer wiring (`sac_tutor` bucket-B chooser: GameLogger hook + main.cpp
      emitter/lambda + index.html panel + DECISIONS.md row + audit manifest; play_invariants
      taught `sacrifice`/`dragon`/`sac_tutor` reply shapes)
- [x] Stage 2d review + 2d-bis audits — `audit_card_fields`: **all 170 cards match the snapshot
      (cost/PT/types/keywords)**; `audit_card_costs`: 0 mismatches (a few Scryfall-429
      transients, all cost-covered by the snapshot check + this session's verbatim 2a paste)
- [x] Stage 3 coverage clean (missing=0, partial=0)
- [x] Stage 4 baseline profile (`Creature Giving.profile.json`)
- [x] Stage 5 verification: `verify_deck.py` **GATE PASS** — coverage/card_fields/viewer/
      viewer_wiring/mismatch/play_invariants all green (only claude_sweep pending recording).
      Explicit harness sweeps: 0 `[nonconv]`, 0 `[fd-diverge]` on seeds 2002+3003 (100 games
      each) plus the gate's 7001/7002 x 60. Multi-depth seed 1001: d0 5.90 (6/200 greedy-unwon
      — DotH stranded without a gift engine, a known d0-greedy limitation; searched depths all
      win) / d3 4.91 / d5 4.86 — monotone + plausible.
      Byte-identity for existing decks: smoke + full regression BOTH ALL PASS (fingerprints
      identical, 0 play changes).
- [x] Stage 5d claude-play sweep (SONNET agents, per user) — see "## Claude-play sweep" below
- [x] Stage 6 report (delivered in-session 2026-08-06)

## Claude-play sweep

- commit: `dd9a6f2` + this change-set (the sweep ran on the pre-fix working tree; both fixes it
  produced are in the same commit as the deck)
- seeds: 9001-9015 (base 9001, gi = i%10) games: 15 launched, **11 completed** (4 aborted on the
  session token cap, not on game issues)
- flags: 0 unresolved
  - FIXED — Crop Rotation's sacrifice-a-land additional cost resolved AFTER the fetched land
    entered in the rollout/ApplyPlan path, so the just-fetched land was offered as a sacrifice
    target (CR 601.2h violation; seeds 9012+9015, two independent repros). Fix: shared
    `PerformSacrificeLandCost` paid BEFORE `PerformLandTutorToBattlefield`; Shard Volley's
    post-resolution path byte-identical (burn smoke fingerprints unchanged).
  - FIXED — tutor reveal disposition label hardcoded "to hand" even for `tutor_to_top`
    (Enlightened/Idyllic Tutor; seed 9005). Fix: label follows the actual placement. This string
    is folded into the play DIGEST, so antilife's fingerprints changed with IDENTICAL play
    (all win turns unchanged; inspected via audit_changed_games) — smoke + regression antilife
    rows re-accepted; overnight antilife rebaseline run started (accept after inspection).
  - PRE-EXISTING (documented, not new) — an unpayable two-colour multi-spell plan can be offered
    (advisory `plan_pays`; engine gracefully reports `dropped_casts`) — the open limitation in
    docs/design/viewer-fixes-2026-07-27.md #1a/#3a + viewer-mana-color-fidelity.md; seed 9008
    reproduced it concretely. Tracked there, not a new defect.
  - 7 further candidate anomalies were verified by the agents against cards.json/source and
    dismissed as false positives (documented win-check granularity, spawn schedules, Priest
    lifegain offsetting City of Brass tap damage, CR 701.19 reshuffles, etc.).
- win turns: claude matched the d5 benchmark in 5/11 games, was 1-2 turns slower in 6 (expected
  vs the clairvoyant search; zero AI-misplay candidates). Drain arithmetic (Priest enters, Wurm
  sweep x2-per-kill, Orchard/War-Riders gifts, Warden lifegain) hand-verified exact by multiple
  agents across full games.

## Post-analysis fix: simultaneous DotH puts + stacking Wurm sweeps (2026-08-06, user-flagged)

The user flagged that a double-Massacre-Wurm Defense of the Heart put must drain 4 per swept
creature (both Wurms see each death) and kill up to toughness 4 (the two −2/−2s stack). Two
defects confirmed and fixed, engine + provider scorer together:

- **Sequential puts → simultaneous** (`PerformUpkeepSacTutor`): previously each put fired its
  cascades before the next entered, so put #1's sweep drained only 2/death (put #2 not yet a
  watcher). Now pass 1 moves every chosen card onto the battlefield, pass 2 fires the shared
  cascades in list order (re-found by per-copy card number — a sweep can erase lower slots).
  Also makes simultaneously-put enter-watchers see each other (paired-Soul-Warden ruling).
- **Sweep collapse → real until-EOT debuff** (`FireOwnEtbTriggers` 1c): −2/−2 now applied via
  `temp_tough_bonus` (cleanup-cleared, sim-key/signature-folded), so same-turn sweeps stack:
  the double put's second trigger kills 3/3 and 4/4 profile spawns at cumulative −4/−4.
- **`SacTutorPutList` scorer mirrored**: all puts' watchers accumulate before any trigger is
  scored (W=4 for the Wurm pair), and `small` became a per-creature toughness-margin list that
  sweeps decrement (gifted tokens appended between sweeps only see later ones). The default
  pick now goes to Wurm+Wurm whenever its 4×kills beats Phantasm+Wurm's 2×(kills+5); Suture
  Priest/Warden pairings are already in the same enumeration for Wurm-exhausted libraries.

Verified: smoke 27/27 + regression 45/45 ALL PASS (byte-identical — both paths are param-gated
to this deck). Probe (d3, seed 1001, 250 games): DotH resolved in 145 games, double-Wurm put
chosen in 98; game_102 upkeep drop 16 → −36 = 13 swept × exactly 4; game_116 sweeps a 3/3
(pre-fix survivor) into a −38 empty board. avg turns 4.91 → 4.864.

## Value-leaf model: built, play-adoption REJECTED, sidecar installed disabled (2026-08-06)

> **SUPERSEDED — ADOPTED AND LIVE 2026-09-03.** The rejection recorded below (and its two
> re-confirmations on 2026-09-03) was a **measurement artifact, not a model defect**. Every one of
> those A/Bs ran the hybrid's heuristic escalation on the **legacy shared-LEFTOVER budget**: the
> deck's `value_play` block carried no `escalation_fresh_frac` key, and an absent key silently meant
> "starved". Instrumented (`MTG_HYBRID_STATS`), the starved escalation committed at **mean depth
> ~1.4** against the pure-heuristic control's **~2.1** — the hybrid was being judged on decisions it
> was never given the budget to make. **The model was never the problem.**
>
> What is live now: `decks/Creature Giving/Creature Giving.value.json` — a freshly regenerated model
> (**11,707 rows** dumped on current play with the deck profile attached, held-out **RMSE 0.475**),
> `value_play.enabled` at **d5/b20** with **`escalation_cap: 5` + beam `W=3` / `beam_leafdepth: 2`**
> (the antilife/hinata2 template), plus `mull_gen_depth 1` / `mull_gen_budget_ms 3` /
> `expected_buckets 20` from the 2026-09-03 phase F. Held-out proof (CG d5/b40, 16 seeds × 500 g =
> 8000 games/arm, one pooled batch): cap+beam at the fresh-full escalation budget is **control
> PARITY — −0.0004 (t=−0.33), zero win↔loss flips in either direction — at 0.43× the control's wall
> (2.3× faster)**. The same config at fresh 0.5 measured +0.0031 (t=+2.71) WORSE, so the budget, not
> the model, was carrying the whole effect.
>
> Engine change adopted the same day: the escalation now **always runs on a FRESH budget equal to the
> full decision budget**, and `value_play.escalation_fresh_frac` was **DELETED as a field** so the
> absent-key-means-starved footgun cannot recur (`MTG_ESCALATION_FRESH_FRAC` survives as a research
> hatch only — default 1.0, `-1` restores legacy).
>
> The parked `Creature Giving.value.DISABLED.json` copy has been **removed** — it is the same model
> that is now live, and git history retains every prior artifact. Full investigation, tables and
> reproduction recipes: **`docs/design/escalation-budget-investigation.md`**.
>
> Everything below is preserved as the historical record of the 2026-08-06 run. Read its verdicts as
> "measured under the starved escalation", not as current status.

Pipeline on frozen `33cb148` (user-directed: value-leaf before the mulligan profile — the
model's purpose is the generation cost lever):
- **Rows**: 2500 games, ONE pooled batch, seeds 900000+ → 11,989 unique rows (searched K=3
  labels under the adopted ladder+B&B path). GBDT (120 trees, depth 4): held-out RMSE
  **0.457 turns**.
- **Matrix** (unbounded, 4 seeds × 400g, incremental): h_conv = **4.7762 at H3** — the
  search SATURATES at depth 3 (H4 = H3 exactly; H8 == V8 == H3-level verified EXACTLY on
  seed 8008, 400 paired games → at full horizon neither leaf ever fires, per the user's
  prediction). The leaf reaches the same plateau at V6–V7 (+0.0007..0.0013, inside tol) but
  not at in-play depths (V5 +0.0038) → `value_trust_depth` UNSET, crossover
  1→1 2→1 3→1 4→2 5→3 6→3 7→3 8→6. Two cells tractability-capped at 50g (H5/9009, V8/11011).
- **Adoption A/B at the play point** (block d5/b20, 4 × 1000g, seeds 600000–603000):
  hybrid slower-to-win on ALL FOUR seeds — 90 slower / 50 faster, net +46 turn-units
  (+0.0115 avg) for a 1.27× wall speedup (58.7s vs 74.4s /1000g). **REJECTED for play.**
  *(2026-09-03: this A/B ran the STARVED escalation — see the superseded banner above. Re-measured
  with the fresh-full budget + cap/beam it is control parity at 0.43× the wall; the rejection does
  not stand.)*
- **Shipped**: `Creature Giving.value.DISABLED.json` (committed, NOT the auto-resolved
  sibling name). First attempt installed it as `<stem>.value.json` with
  `value_play.enabled=false` — the suite went RED: **sidecar PRESENCE activates the
  depth-aware hybrid in play** (`enabled` only releases depth ownership), reproducing the
  A/B regression (d5 s2002 4.792→4.804). The engine resolves strictly `<stem>.value.json`,
  so the DISABLED name is inert (suite-verified green); the mulligan-generation step
  copies/renames it into place when it wants the gen-cost lever. *(2026-09-03: the DISABLED file no
  longer exists — the model is live as `Creature Giving.value.json`, so the copy/rename dance is
  obsolete. The trap itself — sidecar PRESENCE activates the hybrid — still holds.)* Interim-read lesson
  repeated twice in this run: partial matrix cells and 100-game seed subsets both produced
  confident-looking phantom structure ("H4/H5 improve past the plateau") that full counts
  erased.

## Mulligan-profile prep: scout findings + gen wiring (2026-08-07, gen NOT started)

Recommend-mode scouting (`--gen-mulligan recommend`) ran twice and was stopped twice; the
findings and wiring are in place for whoever launches the real gen:

- **Gen settings (user-directed): d3/b10 rollouts + the fetches merged.** Wired via a
  GEN-ONLY `Creature Giving.value.json` carrying `value_play.mull_gen_depth=3` /
  `mull_gen_budget_ms=10` and **no eval_model** (play-inert, deck-restricted smoke verified
  twice; the trained model stays parked in `Creature Giving.value.DISABLED.json` — *superseded
  2026-09-03: the live sidecar now carries the eval_model AND the enabled play block, with
  `mull_gen_depth 1` / `mull_gen_budget_ms 3` re-derived by the 2026-09-03 regen*). GOTCHA
  found: the profile loader ignores the whole `value_play` block unless a `target_depth`
  key is present — the sidecar carries `target_depth:0` for that reason. Launch with
  `MTG_EQUIV_FORCE_MERGE="Windswept Heath, Misty Rainforest"` — at d3 discovery the
  fetches merge on their own (21 raw buckets), but pinning the spec keeps the bucket_fp
  contract explicit.
- **Measured scout economics** (this box, 24 threads): d5/b20 sustained 41–44 rollouts/s
  (1,243,366 tasks — an ~8 h scout; `complete` R40 infeasible). d3/b10 + merge: 21 buckets,
  789,092 distinct hands × 2 pd = 1,578,184 tasks, 122/s first-minute decaying to ~78/s
  sustained → **R=1 floor pass ≈ 5.5–6.5 h**; rollout-config play digest `d8f71a4f27ed93ee`
  (pooling parity). The adaptive complete/fast projection requires letting the scout FINISH
  (stopped at 24% for the slow-stream default below; no slow-cell list was captured).
- **`MTG_KEEP_SLOW_MS` now defaults ON at 30 s** (user-directed): degenerate rollouts stream
  live with the reproducing seed instead of surfacing only in the end-of-run top-12.
  `=0` still disables; explicit values override. Analyzer-only, results unchanged.
- **Next step when resuming:** relaunch the scout (same launch line as above), read its
  complete/fast projection + slowest cells, pick a recipe (or the value-leaf gen lever /
  secondary-machine chunking if projections bust the ~8 h window), then generate on a
  frozen commit per the mulligan-profile skill.

## CreatureGivingProvider: Orchard-first land tutoring (ADOPTED 2026-08-06, user-directed)

`CreatureGivingProvider` (routed by the `gift` detection in `SelectDecisionProvider`, formerly
GenericProvider) overrides `TutorCandidates` for the land-typed tutors only (Sylvan Scrying
to-hand, Crop Rotation to-battlefield): **while a Forbidden Orchard remains in the library the
single candidate is Forbidden Orchard** — one cast variant, no search axis. Per the user: 0
exceptions, the narrowing lives in the provider; `MTG_UNPRUNED` / `MTG_UNPRUNE=tutor` remains
the standing full-list lever (verified: the tutor-gate arm reproduces the pre-provider batch
digests byte-for-byte). No Orchard left → Generic full list returns (search picks). The
non-land Enlightened Tutor keeps the full searched list. Fetchlands unaffected (Orchard has
no basic types). Inherits Generic elsewhere (incl. the root SacTutorPutList).

Measured (with vs without, per-game; unwon = max_turns+1):

| arm | without | with | slower | faster | net turn-units |
|---|---|---|---|---|---|
| d0 s1001 ×1000 | 5.9640 | 5.7690 | 16 | 145 | −195 |
| d3 s1001 ×250 | 4.8640 | 4.8160 | 4 | 16 | −12 |
| d3 s2002 ×250 | 4.8440 | 4.8040 | 4 | 14 | −10 |
| d3 s3003 ×250 | 4.7840 | 4.7600 | 9 | 14 | −6 |
| d5 s1001 ×150 | 4.8800 | 4.8000 | 1 | 13 | −12 |
| held-out d3 s4004 ×250 | 4.9080 | 4.8960 | 8 | 11 | −3 |
| held-out d3 s5005 ×250 | 4.8400 | 4.8040 | 7 | 16 | −9 |
| held-out d5 s4004 ×150 | 4.9533 | 4.9200 | 3 | 8 | −5 |

Every arm (train + held-out) net-improves. USER-CORRECTED mechanism analysis (2026-08-06):
there is NO genuine land-quality reason to fetch anything else — every land in the deck taps
for exactly one mana and Orchard is any-colour, so no fetch accelerates or fixes better. (The
sole arguable exception, Azorius Chancery's 2-mana-per-card, matters only in the narrow
"turn 3, no further land drop in hand" case — and it was fetched in ZERO inspected games,
either arm.) The slower games decompose, verified with the MTG_SHUFFLE_SALT_SEARCH
clairvoyance-decoupling instrument (salt 12345, d3 s1001 + s3003, both arms):
- **Clairvoyance** — d3 s1001's four slower games ALL collapse to equal-or-faster when the
  search can no longer foresee the real reshuffle: the full-list arm was picking fetch
  targets for the post-shuffle library order it could see, not for the lands themselves.
- **Plan-set churn** — s3003 gi27 flips (4→6) with NO tutor cast in either arm, and gi235
  flips with IDENTICAL fetches in both arms: a tutor in hand enumerates 1 vs ~8 cast
  variants, and that plan-set difference alone tips unrelated search tie-breaks.
- **Real-draw divergence** — a different fetch target changes library composition and
  shuffle count, so the two arms simply draw different games afterward; arm-luck that sums
  AGAINST the full list overall.
On the clairvoyance-stripped metric Orchard-first stays net-better on both seeds (s1001
−33, s3003 −6 turn-units; both arms individually worse without clairvoyance, as expected).
Smoke 27/27 + regression 45/45 ALL PASS (byte-identical elsewhere).

## Approved deferrals

- `viewer:auditor` — Crop Rotation `sacrifice` HARD MISS is an **auditor artifact; the decision
  is verified surfaced**. The CR-601.2h timing fix (sacrifice paid BEFORE the fetched land enters) means a
  cast with only ONE land legally offers no choice, and the auditor's seed-search biases toward
  the EARLIEST cast of the {G} instant — turn 1 with exactly one land — so its sweep never
  reaches a multi-land cast. Targeted repro (the 5d sweep game, seed 9012 gi 1, choices
  `0,1,0,1,24,0,0,3`) confirms the `sacrifice` decision fires at turn 5 with the two
  PRE-existing lands as options and the about-to-be-fetched land correctly absent. Wiring is
  live (viewer_wiring PASS); the pre-fix auditor run also surfaced it (with the now-fixed
  illegal extra option). Signed off 2026-08-06.

(other modelling collapses listed above — Massacre Wurm -2/-2 kill-collapse, War-Riders
always-pay, always-taken "may" triggers, unblockable/trample inert — are presented for user
sign-off in the Stage 6 report.)

<!-- verify_deck:begin (generated -- do not edit inside) -->
## Last verification (2026-08-06)

`verify_deck.py decks/Creature Giving/Creature Giving.cod --no-network --write-ledger` -> **PASS**

| Gate | Status | Blocking | Summary |
|---|---|---|---|
| coverage | PASS | yes | all 23 cards full (missing=0, partial=0) |
| card_costs | SKIP | yes | skipped (--no-network) |
| card_fields | PASS | yes | 170 cards match snapshot (cost/PT/types/keywords); 5 allowlisted divergence(s) |
| clause_ledger | SKIP | no | covered by coverage+bracket-notes+oracle-diff |
| viewer | DEFERRED | yes | auditor rc=1:  | HARD MISS -- card WAS cast but its decision never surfaced (silently heuristic-resolved; go back to Stage 2c-ter and wire it): |   Crop Rotation: expected 'sacrifice' -> NOT surfaced |
| viewer_wiring | PASS | yes | 4 type(s) wired (emitter + GUI): bounce, land_entry, sac_tutor, sacrifice |
| mismatch | PASS | yes | no nonconv/fd-diverge across seeds [7001, 7002] x 60 games |
| play_invariants | PASS | yes | 8 game(s)/104 decisions: determinism+integrity+progress hold |
| claude_sweep | PASS | yes | Claude-play sweep recorded, 0 unresolved flags |

### Pending user sign-off (block the gate until fixed OR approved below)
_none_ -- every blocking gate is green or already signed off.

### Stage 6a disclosure (deferrals + not-yet-built checks)
- coverage deferral -- Forbidden Orchard: 5-colour land + the Spirit trigger (taps_spawn_opp_token). Modelled per the deck's actual play: assume the active player taps each Orchard for mana EVERY turn it is in play, so the opponent gets one 1/1 colourless Spirit per Orchard per turn -- created at turn start for copies already in play + on-play for a freshly-played copy (SpawnForbiddenOrchardTokensTurnStart / on-play hook, lockstep in executor + rollout). The Spirits are real opponent creatures, so they are first-class Soulfire-dig / Crackle-discount / removal targets (counted by SoulfireTargetCount, HinataAvailableTargets, FindOpponentCreature). In the passive-opponent goldfish (Hinata flies) they never block -- pure targets. NOT modelled: extra Spirits from Reality Spasm RE-tapping Orchards within a turn (RS is floating-mana, no literal re-tap) -- deferred to the combo heuristic where the Soulfire+RS line is evaluated.
- coverage deferral -- Hunted Phantasm: "Can't be blocked" is inert vs the passive goldfish opponent (it never blocks). The ETB gift is etb_opp_creates_tokens 5: five real opponent 1/1 Goblin tokens created through the shared enter cascade, so each drains per Suture Priest, counts toward Defense of the Heart's condition, and dies to a Massacre Wurm sweep. Single opponent -> "target opponent" is no choice.
- coverage deferral -- Suture Priest: Both "you may" triggers are always taken (strictly beneficial): own_creature_enters_lifegain 1 + opp_creature_enters_life_loss 1 (life LOSS, not damage), fired by the shared enter-watcher cascade for every creature entering on either side (gift tokens, Orchard Spirits, opponent spawns). Multiple copies stack.
- coverage deferral -- Massacre Wurm: ETB -2/-2-until-EOT collapsed to "destroy each opponent creature with toughness - damage <= 2 at ETB" (etb_opp_creatures_debuff 2) -- equivalent in goldfish: opponent creatures never block/attack or get buffs, so the debuff on survivors is inert and only the kills matter. Death drain via opp_dies_life_loss 2, fired at every opponent-creature death site; the Wurm counts its own sweep's kills (it is on the battlefield when its ETB resolves). Multiple Wurms stack per death.
- coverage deferral -- Reflecting Pool: Modelled faithfully via the `reflecting` flag: its colours are the runtime UNION of the controller's OTHER non-reflecting lands (EffectiveProduces), and it produces NOTHING when it is the only land (so a solo / all-Reflecting-Pool hand is manaless -> mulligan). The static `produces` below is IGNORED for a reflecting source; it is kept only as documentation of the deck's colour identity.
- coverage deferral -- Soul Warden: any_creature_enters_lifegain 1 -- fires for every OTHER creature entering on EITHER side (including the deck's own gift tokens entering under the opponent). Our lifegain is goldfish-inert for the clock (opponent life is the wincon) but faithful as a resource vs our own pain/shock/fetch life costs.
- coverage deferral -- Essence Warden: any_creature_enters_lifegain 1 -- same model as Soul Warden: fires for every OTHER creature entering on either side; lifegain is a resource vs our own pain/shock/fetch life costs, not a clock.
- coverage deferral -- City of Brass: WUBRG pain land: tap_self_damage 1 (mandatory whenever tapped for mana, applied at the shared tap sites). Our life loss is inert for the goldfish clock; it interacts only with our own life-cost accounting.
- coverage deferral -- Defense of the Heart: upkeep_sac_tutor_creatures 2 + upkeep_sac_tutor_opp_min 3; the intervening-if is checked at the upkeep (trigger time == resolution time in this engine). WHICH creatures + their enter ORDER = DecisionProvider::SacTutorPutList (default: closed-form immediate-drain maximisation -- token-makers enter before sweepers); a human overrides via the sac_tutor chooser. Each put creature resolves its own ETB through the shared cascades (a put Hunted Phantasm gifts, a put Massacre Wurm sweeps), then the library is shuffled.
- coverage deferral -- Sylvan Scrying: tutor_to_hand + tutor_types [Land
- coverage deferral -- Windswept Heath: Resolved by PerformFetch: pulls a chosen library land (FetchCandidates colour heuristic), that land enters resolving its own shock/tapped choice, pay 1 life, fetchland to graveyard. Keeps a W/B/R/G `produces` ONLY so a copy in HAND counts as a flexible colour source for fixing heuristics; it never taps for mana (never reaches the battlefield). Library order/thinning past the fetched card not modelled (goldfish-irrelevant).
- coverage deferral -- Crop Rotation: sacrifice_land = the shared additional-cost path (WHICH land = provider SacrificeLandCandidates, human chooser override) + tutor_land_to_battlefield: the searched tutor axis picks the land, which enters resolving its own shock/enters-tapped choice; a fetched Forbidden Orchard spawns its opponent Spirit on entry (it is tapped for mana this same turn, mirroring the on-play hook).
- coverage deferral -- Enlightened Tutor: Tutor target: SEARCHED by default (pending); this deck uses the enabler_then_wincon heuristic override (Tainted Remedy first, else Aria). Placed on top -> drawn next turn.
- coverage deferral -- Varchild's War-Riders: Cumulative upkeep modelled ALWAYS PAID (cumulative_upkeep_opp_token + Permanent::age_counters): +1 age counter at each of our upkeeps, then the opponent creates age x 1/1 red Survivor through the enter-watcher cascade. Always paying is weakly dominant vs the passive opponent (the gifts only feed our drains / Defense of the Heart and the 3/4 body is kept) -- pay-vs-sacrifice is a disclosed auto-decision, not a surfaced choice. Trample and rampage are inert (the opponent never blocks).
- coverage deferral -- Azorius Chancery: Karoo bounce land: enters tapped, makes 2 mana ({W}{U}, modelled as wild like other duals), and on ETB returns one of your lands to hand (BounceKarooLand prefers a tapped land so no mana is lost this turn; the returned land must be replayed, the real tempo cost).
- coverage deferral -- Tree of Tales: Artifact land: a legal Enlightened Tutor target (type Artifact), otherwise a plain {G} source. No other artifact synergy exists in this deck.
- coverage deferral -- Misty Rainforest: Resolved by PerformFetch: pulls a chosen library land (FetchCandidates colour heuristic), that land enters resolving its own shock/tapped choice, pay 1 life, fetchland to graveyard. Keeps a W/U/B/R/G `produces` ONLY so a copy in HAND counts as a flexible colour source for fixing heuristics (this deck's Forest/Island-typed targets reach all five colours via its duals); it never taps for mana (never reaches the battlefield).
- card_costs SKIPPED (--no-network) -- Scryfall cost/cmc reality-diff not run
- allowlisted divergence -- Galerider Sliver [keywords]: Keyword-lord: 'Sliver creatures you control have flying' grants flying to your Slivers INCLUDING itself, so the card functionally has flying (modeled 
- allowlisted divergence -- Striking Sliver [keywords]: Keyword-lord: grants first strike to your Slivers incl. itself (modeled self-innate). First strike is inert in goldfishing (no blockers). See oracle b
- allowlisted divergence -- Cloudshredder Sliver [keywords]: Keyword-lord: grants flying+haste to your Slivers incl. itself. Flying self-innate + inert in goldfishing; haste additionally granted to other Slivers
- allowlisted divergence -- Haytham Kenway [keywords]: 'Protection from Assassins' is a real keyword but inert in goldfishing (no Assassins in play); the protection-to-other-Knights is an anthem grant, not
- allowlisted divergence -- Goblin Piledriver [keywords]: 'Protection from blue' is a real keyword but inert in goldfishing (the passive opponent has no blue sources or blockers to target); the attack-trigger
- oracle_text advisory -- Light Up the Stage: oracle_text diverges (similarity 0.69); scryfall='Spectacle {R} (You may cast this spell for its spectacle cost rather than its mana cost if an opponent lost li
- oracle_text advisory -- Crystalline Sliver: oracle_text diverges (similarity 0.61); scryfall="All Slivers have shroud. (They can't be the targets of spells or abilities.)"
- oracle_text advisory -- Galerider Sliver: oracle_text diverges (similarity 0.41); scryfall='Sliver creatures you control have flying.'
- oracle_text advisory -- Striking Sliver: oracle_text diverges (similarity 0.56); scryfall='Sliver creatures you control have first strike. (They deal combat damage before creatures without first strike
- oracle_text advisory -- Cloudshredder Sliver: oracle_text diverges (similarity 0.48); scryfall='Sliver creatures you control have flying and haste.'
- oracle_text advisory -- Hibernation Sliver: oracle_text diverges (similarity 0.49); scryfall='All Slivers have "Pay 2 life: Return this permanent to its owner\'s hand."'
- oracle_text advisory -- Cavern of Souls: oracle_text diverges (similarity 0.75); scryfall="As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Unclaimed Territory: oracle_text diverges (similarity 0.75); scryfall='As this land enters, choose a creature type.\n{T}: Add {C}.\n{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Mutavault: oracle_text diverges (similarity 0.56); scryfall="{T}: Add {C}.\n{1}: This land becomes a 2/2 creature with all creature types until end of turn. It's still a l
- oracle_text advisory -- Aether Vial: oracle_text diverges (similarity 0.71); scryfall='At the beginning of your upkeep, you may put a charge counter on this artifact.\n{T}: You may put a creature c
- oracle_text advisory -- Reliquary Tower: oracle_text diverges (similarity 0.44); scryfall='You have no maximum hand size.\n{T}: Add {C}.'
- oracle_text advisory -- Dwarven Hold: oracle_text diverges (similarity 0.23); scryfall='This land enters tapped.\nYou may choose not to untap this land during your untap step.\nAt the beginning of y
- oracle_text advisory -- Mercadian Bazaar: oracle_text diverges (similarity 0.26); scryfall='This land enters tapped.\n{T}: Put a storage counter on this land.\n{T}, Remove any number of storage counters
- oracle_text advisory -- Temple of Epiphany: oracle_text diverges (similarity 0.60); scryfall='This land enters tapped.\nWhen this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Thundering Falls: oracle_text diverges (similarity 0.63); scryfall='({T}: Add {U} or {R}.)\nThis land enters tapped.\nWhen this land enters, surveil 1. (Look at the top card of y
- oracle_text advisory -- Land's Edge: oracle_text diverges (similarity 0.51); scryfall='Discard a card: If the discarded card was a land card, this enchantment deals 2 damage to target player or pla
- oracle_text advisory -- Throes of Chaos: oracle_text diverges (similarity 0.06); scryfall='Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland card tha
- oracle_text advisory -- Tournament Grounds: oracle_text diverges (similarity 0.37); scryfall='{T}: Add {C}.\n{T}: Add {R}, {W}, or {B}. Spend this mana only to cast a Knight or Equipment spell.'
- oracle_text advisory -- Dauntless Bodyguard: oracle_text diverges (similarity 0.55); scryfall='As this creature enters, choose another creature you control.\nSacrifice this creature: The chosen creature ga
- oracle_text advisory -- Venerable Knight: oracle_text diverges (similarity 0.52); scryfall='When this creature dies, put a +1/+1 counter on target Knight you control.'
- oracle_text advisory -- Worthy Knight: oracle_text diverges (similarity 0.45); scryfall='Whenever you cast a Knight spell, create a 1/1 white Human creature token.'
- oracle_text advisory -- Acclaimed Contender: oracle_text diverges (similarity 0.77); scryfall='When this creature enters, if you control another Knight, look at the top five cards of your library. You may 
- oracle_text advisory -- Knight Exemplar: oracle_text diverges (similarity 0.41); scryfall='First strike (This creature deals combat damage before creatures without first strike.)\nOther Knight creature
- oracle_text advisory -- Marshal of Zhalfir: oracle_text diverges (similarity 0.49); scryfall='Other Knights you control get +1/+1.\n{W}{U}, {T}: Tap another target creature.'
- oracle_text advisory -- Haytham Kenway: oracle_text diverges (similarity 0.53); scryfall='Protection from Assassins\nOther Knights you control get +2/+2 and have protection from Assassins.\nWhen Hayth
- oracle_text advisory -- Adeline, Resplendent Cathar: oracle_text diverges (similarity 0.76); scryfall="Vigilance\nAdeline's power is equal to the number of creatures you control.\nWhenever you attack, for each opp
- oracle_text advisory -- Windswept Heath: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Plains card, put it onto the battlef
- oracle_text advisory -- Marsh Flats: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Swamp card, put it onto the battlefi
- oracle_text advisory -- Bloodstained Mire: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Mountain card, put it onto the battle
- oracle_text advisory -- Wooded Foothills: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Mountain or Forest card, put it onto the battl
- oracle_text advisory -- Grove of the Burnwillows: oracle_text diverges (similarity 0.20); scryfall='{T}: Add {C}.\n{T}: Add {R} or {G}. Each opponent gains 1 life.'
- oracle_text advisory -- Skyshroud Cutter: oracle_text diverges (similarity 0.66); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 5 life."
- oracle_text advisory -- Plague Drone: oracle_text diverges (similarity 0.70); scryfall='Flying\nRot Fly — If an opponent would gain life, that player loses that much life instead.'
- oracle_text advisory -- Aria of Flame: oracle_text diverges (similarity 0.78); scryfall='When this enchantment enters, each opponent gains 10 life.\nWhenever you cast an instant or sorcery spell, put
- oracle_text advisory -- Fiery Justice: oracle_text diverges (similarity 0.54); scryfall='Fiery Justice deals 5 damage divided as you choose among any number of targets. Target opponent gains 5 life.'
- oracle_text advisory -- Swords to Plowshares: oracle_text diverges (similarity 0.44); scryfall='Exile target creature. Its controller gains life equal to its power.'
- oracle_text advisory -- Invigorate: oracle_text diverges (similarity 0.61); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have an opponent gain 3 life.\nTarget
- oracle_text advisory -- Reverent Silence: oracle_text diverges (similarity 0.57); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 6 life.\n
- oracle_text advisory -- Idyllic Tutor: oracle_text diverges (similarity 0.43); scryfall='Search your library for an enchantment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Enlightened Tutor: oracle_text diverges (similarity 0.55); scryfall='Search your library for an artifact or enchantment card, reveal it, then shuffle and put that card on top.'
- oracle_text advisory -- Forbidden Orchard: oracle_text diverges (similarity 0.22); scryfall='{T}: Add one mana of any color.\nWhenever you tap this land for mana, target opponent creates a 1/1 colorless 
- oracle_text advisory -- Reflecting Pool: oracle_text diverges (similarity 0.26); scryfall='{T}: Add one mana of any type that a land you control could produce.'
- oracle_text advisory -- Izzet Signet: oracle_text diverges (similarity 0.11); scryfall='{1}, {T}: Add {U}{R}.'
- oracle_text advisory -- Ponder: oracle_text diverges (similarity 0.32); scryfall='Look at the top three cards of your library, then put them back in any order. You may shuffle.\nDraw a card.'
- oracle_text advisory -- Preordain: oracle_text diverges (similarity 0.27); scryfall='Scry 2, then draw a card. (To scry 2, look at the top two cards of your library, then put any number of them o
- oracle_text advisory -- Expressive Iteration: oracle_text diverges (similarity 0.45); scryfall='Look at the top three cards of your library. Put one of them into your hand, put one of them on the bottom of 
- oracle_text advisory -- Crackle with Power: oracle_text diverges (similarity 0.20); scryfall='Crackle with Power deals five times X damage to each of up to X targets.'
- oracle_text advisory -- Remand: oracle_text diverges (similarity 0.58); scryfall="Counter target spell. If that spell is countered this way, put it into its owner's hand instead of into that p
- oracle_text advisory -- Memory Lapse: oracle_text diverges (similarity 0.69); scryfall="Counter target spell. If that spell is countered this way, put it on top of its owner's library instead of int
- oracle_text advisory -- Distorting Wake: oracle_text diverges (similarity 0.34); scryfall="Return X target nonland permanents to their owners' hands."
- oracle_text advisory -- Icy Blast: oracle_text diverges (similarity 0.64); scryfall="Tap X target creatures.\nFerocious — If you control a creature with power 4 or greater, those creatures don't 
- oracle_text advisory -- Hinata, Dawn-Crowned: oracle_text diverges (similarity 0.31); scryfall='Flying, trample\nSpells you cast cost {1} less to cast for each target.\nSpells your opponents cast cost {1} m
- oracle_text advisory -- Izzet Boilerworks: oracle_text diverges (similarity 0.42); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {U}{
- oracle_text advisory -- Soulfire Eruption: oracle_text diverges (similarity 0.30); scryfall="Choose any number of target creatures, planeswalkers, and/or players. For each of them, exile the top card of 
- oracle_text advisory -- Magma Opus: oracle_text diverges (similarity 0.46); scryfall='Magma Opus deals 4 damage divided as you choose among any number of targets. Tap two target permanents. Create
- oracle_text advisory -- Reality Spasm: oracle_text diverges (similarity 0.20); scryfall='Choose one —\n• Tap X target permanents.\n• Untap X target permanents.'
- oracle_text advisory -- Ornithopter of Paradise: oracle_text diverges (similarity 0.13); scryfall='Flying\n{T}: Add one mana of any color.'
- oracle_text advisory -- Gamble: oracle_text diverges (similarity 0.22); scryfall='Search your library for a card, put that card into your hand, discard a card at random, then shuffle.'
- oracle_text advisory -- Irencrag Feat: oracle_text diverges (similarity 0.14); scryfall='Add seven {R}. You can cast only one more spell this turn.'
- oracle_text advisory -- Pyretic Ritual: oracle_text diverges (similarity 0.06); scryfall='Add {R}{R}{R}.'
- oracle_text advisory -- Seething Song: oracle_text diverges (similarity 0.08); scryfall='Add {R}{R}{R}{R}{R}.'
- oracle_text advisory -- Desperate Ritual: oracle_text diverges (similarity 0.18); scryfall="Add {R}{R}{R}.\nSplice onto Arcane {1}{R} (As you cast an Arcane spell, you may reveal this card from your han
- oracle_text advisory -- Dragonlord Kolaghan: oracle_text diverges (similarity 0.53); scryfall='Flying, haste\nOther creatures you control have haste.\nWhenever an opponent casts a creature or planeswalker 
- oracle_text advisory -- Karrthus, Tyrant of Jund: oracle_text diverges (similarity 0.37); scryfall='Flying, haste\nWhen Karrthus enters, gain control of all Dragons, then untap all Dragons.\nOther Dragon creatu
- oracle_text advisory -- Ruby Medallion: oracle_text diverges (similarity 0.17); scryfall='Red spells you cast cost {1} less to cast.'
- oracle_text advisory -- Lotus Bloom: oracle_text diverges (similarity 0.25); scryfall='Suspend 3—{0} (Rather than cast this card from your hand, pay {0} and exile it with three time counters on it.
- oracle_text advisory -- Rite of Flame: oracle_text diverges (similarity 0.21); scryfall='Add {R}{R}, then add {R} for each card named Rite of Flame in each graveyard.'
- oracle_text advisory -- Scourge of Valkas: oracle_text diverges (similarity 0.32); scryfall='Flying\nWhenever this creature or another Dragon you control enters, it deals X damage to any target, where X 
- oracle_text advisory -- Lathliss, Dragon Queen: oracle_text diverges (similarity 0.31); scryfall='Flying\nWhenever another nontoken Dragon you control enters, create a 5/5 red Dragon creature token with flyin
- oracle_text advisory -- Utvara Hellkite: oracle_text diverges (similarity 0.24); scryfall='Flying\nWhenever a Dragon you control attacks, create a 6/6 red Dragon creature token with flying.'
- oracle_text advisory -- Dragonstorm: oracle_text diverges (similarity 0.16); scryfall='Search your library for a Dragon permanent card, put it onto the battlefield, then shuffle.\nStorm (When you c
- oracle_text advisory -- Apex of Power: oracle_text diverges (similarity 0.15); scryfall='Exile the top seven cards of your library. Until end of turn, you may cast spells from among them.\nIf this sp
- oracle_text advisory -- Slippery Bogle: oracle_text diverges (similarity 0.41); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Gladecover Scout: oracle_text diverges (similarity 0.76); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Kor Spiritdancer: oracle_text diverges (similarity 0.53); scryfall='This creature gets +2/+2 for each Aura attached to it.\nWhenever you cast an Aura spell, you may draw a card.'
- oracle_text advisory -- Light-Paws, Emperor's Voice: oracle_text diverges (similarity 0.74); scryfall='Whenever an Aura you control enters, if you cast it, you may search your library for an Aura card with mana va
- oracle_text advisory -- Ethereal Armor: oracle_text diverges (similarity 0.60); scryfall='Enchant creature\nEnchanted creature gets +1/+1 for each enchantment you control and has first strike.'
- oracle_text advisory -- Rancor: oracle_text diverges (similarity 0.68); scryfall="Enchant creature\nEnchanted creature gets +2/+0 and has trample.\nWhen this Aura is put into a graveyard from 
- oracle_text advisory -- Daybreak Coronet: oracle_text diverges (similarity 0.58); scryfall='Enchant creature with another Aura attached to it\nEnchanted creature gets +3/+3 and has first strike, vigilan
- oracle_text advisory -- Armadillo Cloak: oracle_text diverges (similarity 0.77); scryfall='Enchant creature\nEnchanted creature gets +2/+2 and has trample.\nWhenever enchanted creature deals damage, yo
- oracle_text advisory -- Spirit Mantle: oracle_text diverges (similarity 0.66); scryfall='Enchant creature\nEnchanted creature gets +1/+1 and has protection from creatures.'
- oracle_text advisory -- Spider Umbra: oracle_text diverges (similarity 0.40); scryfall='Enchant creature\nEnchanted creature gets +1/+1 and has reach. (It can block creatures with flying.)\nUmbra ar
- oracle_text advisory -- Ancestral Mask: oracle_text diverges (similarity 0.59); scryfall='Enchant creature\nEnchanted creature gets +2/+2 for each other enchantment on the battlefield.'
- oracle_text advisory -- Alpha Authority: oracle_text diverges (similarity 0.54); scryfall="Enchant creature\nEnchanted creature has hexproof and can't be blocked by more than one creature."
- oracle_text advisory -- Gryff's Boon: oracle_text diverges (similarity 0.75); scryfall='Enchant creature\nEnchanted creature gets +1/+0 and has flying.\n{3}{W}: Return this card from your graveyard 
- oracle_text advisory -- Audacity: oracle_text diverges (similarity 0.59); scryfall="Enchant creature\nEnchanted creature gets +2/+0 and has trample. (It can deal excess combat damage to the play
- oracle_text advisory -- All That Glitters: oracle_text diverges (similarity 0.57); scryfall='Enchant creature\nEnchanted creature gets +1/+1 for each artifact and/or enchantment you control.'
- oracle_text advisory -- Spirit Link: oracle_text diverges (similarity 0.47); scryfall='Enchant creature (Target a creature as you cast this. This card enters attached to that creature.)\nWhenever e
- oracle_text advisory -- Lion Umbra: oracle_text diverges (similarity 0.80); scryfall='Enchant modified creature (Equipment, Auras its controller controls, and counters are modifications.)\nEnchant
- oracle_text advisory -- Brushland: oracle_text diverges (similarity 0.34); scryfall='{T}: Add {C}.\n{T}: Add {G} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Branchloft Pathway: oracle_text diverges (similarity 0.00); scryfall=''
- oracle_text advisory -- Goblin King: oracle_text diverges (similarity 0.31); scryfall='Other Goblins get +1/+1 and have mountainwalk.'
- oracle_text advisory -- Goblin Chieftain: oracle_text diverges (similarity 0.41); scryfall='Haste (This creature can attack and {T} as soon as it comes under your control.)\nOther Goblin creatures you c
- oracle_text advisory -- Goblin Warchief: oracle_text diverges (similarity 0.52); scryfall='Goblin spells you cast cost {1} less to cast.\nGoblins you control have haste.'
- oracle_text advisory -- Goblin Piledriver: oracle_text diverges (similarity 0.43); scryfall="Protection from blue (This creature can't be blocked, targeted, dealt damage, or enchanted by anything blue.)\
- oracle_text advisory -- Goblin Matron: oracle_text diverges (similarity 0.66); scryfall='When this creature enters, you may search your library for a Goblin card, reveal that card, put it into your h
- oracle_text advisory -- Mogg War Marshal: oracle_text diverges (similarity 0.56); scryfall='Echo {1}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Siege-Gang Commander: oracle_text diverges (similarity 0.58); scryfall='When this creature enters, create three 1/1 red Goblin creature tokens.\n{1}{R}, Sacrifice a Goblin: This crea
- oracle_text advisory -- Skirk Prospector: oracle_text diverges (similarity 0.31); scryfall='Sacrifice a Goblin: Add {R}.'
- oracle_text advisory -- Krenko, Mob Boss: oracle_text diverges (similarity 0.43); scryfall='{T}: Create X 1/1 red Goblin creature tokens, where X is the number of Goblins you control.'
- oracle_text advisory -- Pashalik Mons: oracle_text diverges (similarity 0.52); scryfall='Whenever Pashalik Mons or another Goblin you control dies, Pashalik Mons deals 1 damage to any target.\n{3}{R}
- oracle_text advisory -- Rundvelt Hordemaster: oracle_text diverges (similarity 0.36); scryfall="Other Goblins you control get +1/+1.\nWhenever this creature or another Goblin you control dies, exile the top
- oracle_text advisory -- Goblin Lackey: oracle_text diverges (similarity 0.56); scryfall='Whenever this creature deals damage to a player, you may put a Goblin permanent card from your hand onto the b
- oracle_text advisory -- Muxus, Goblin Grandee: oracle_text diverges (similarity 0.08); scryfall='When Muxus enters, reveal the top six cards of your library. Put all Goblin creature cards with mana value 5 o
- oracle_text advisory -- Goblin Chainwhirler: oracle_text diverges (similarity 0.40); scryfall='First strike\nWhen this creature enters, it deals 1 damage to each opponent and each creature and planeswalker
- oracle_text advisory -- Twinshot Sniper: oracle_text diverges (similarity 0.50); scryfall='Reach\nWhen this creature enters, it deals 2 damage to any target.\nChannel — {1}{R}, Discard this card: It de
- oracle_text advisory -- Stingscourger: oracle_text diverges (similarity 0.71); scryfall="Echo {3}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Three Tree City: oracle_text diverges (similarity 0.47); scryfall='As Three Tree City enters, choose a creature type.\n{T}: Add {C}.\n{2}, {T}: Choose a color. Add an amount of 
- oracle_text advisory -- Hunted Phantasm: oracle_text diverges (similarity 0.34); scryfall="This creature can't be blocked.\nWhen this creature enters, target opponent creates five 1/1 red Goblin creatu
- oracle_text advisory -- Suture Priest: oracle_text diverges (similarity 0.49); scryfall='Whenever another creature you control enters, you may gain 1 life.\nWhenever a creature an opponent controls e
- oracle_text advisory -- Massacre Wurm: oracle_text diverges (similarity 0.38); scryfall='When this creature enters, creatures your opponents control get -2/-2 until end of turn.\nWhenever a creature 
- oracle_text advisory -- Soul Warden: oracle_text diverges (similarity 0.25); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- Essence Warden: oracle_text diverges (similarity 0.34); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- City of Brass: oracle_text diverges (similarity 0.45); scryfall='Whenever this land becomes tapped, it deals 1 damage to you.\n{T}: Add one mana of any color.'
- oracle_text advisory -- Defense of the Heart: oracle_text diverges (similarity 0.43); scryfall='At the beginning of your upkeep, if an opponent controls three or more creatures, sacrifice this enchantment, 
- oracle_text advisory -- Sylvan Scrying: oracle_text diverges (similarity 0.47); scryfall='Search your library for a land card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Crop Rotation: oracle_text diverges (similarity 0.42); scryfall='As an additional cost to cast this spell, sacrifice a land.\nSearch your library for a land card, put that car
- oracle_text advisory -- Varchild's War-Riders: oracle_text diverges (similarity 0.58); scryfall='Cumulative upkeep—Have an opponent create a 1/1 red Survivor creature token. (At the beginning of your upkeep,
- oracle_text advisory -- Azorius Chancery: oracle_text diverges (similarity 0.42); scryfall="This land enters tapped.\nWhen this land enters, return a land you control to its owner's hand.\n{T}: Add {W}{
- oracle_text advisory -- Tree of Tales: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Misty Rainforest: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Island card, put it onto the battlef
- clause_ledger: no dedicated per-clause artifact. Its function -- every oracle clause modeled/inert/deferred -- is covered by coverage(partial hard-stop) + bracket-note deferrals + viewer oracle cross-check + audit_card_fields oracle-diff. A dedicated ledger is deferred (high per-card cost, marginal added rigor).
- viewer DEFERRED (user sign-off): audit_viewer_decisions rc=1:  | HARD MISS -- card WAS cast but its decision never surfaced (silently heuristic-resolved; go back to Stage 2c-ter and wire it): |   Crop Rotation: expected 'sacrifice' -> NOT surfaced
- claude_sweep recorded at commit dd9a6f2 (HEAD d34c0e6d56f6); re-run if play changed since (play_invariants + smoke digests track play live).

<!-- verify_deck:end -->
