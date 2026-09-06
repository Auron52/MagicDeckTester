# The interior second main is SEARCHED, unconditionally (USER directive, 2026-09-05)

**Status: shipping. This document is the rule; the per-deck opt-in era it replaces is over.**

## The rule

`SolveSecondMainInSearch`'s BRANCH site — the search's own "what does passing the pre-combat main
buy me" pricing — runs `SearchedSecondMainMemoized`, always, for every deck, at every depth
(`depth <= 0` runs it at one ply). There is no greedy fallback at the branch site, no global
lever, and no per-deck opt-in. A new deck gets searched interior second mains on day one with
zero provider code.

The USER's words, assembled from the directives this implements:

* 2026-08-09: *"we can't afford to have second main be greedy ... I want no greedy steps in the
  middle of the search."*
* 2026-08-23: *"decks must have NO GREEDY components in the search."*
* 2026-09-05: *"Can we delete that greedy interior? I don't want it to exist for any future
  decks."* — *"I went through a lot of effort working with agents to stop using it for existing
  decks and I don't want to keep revisiting it."* — *"As apparently every new deck has this
  issue."*

The structural flaw being fixed: the searched path was built OPT-IN (per-deck
`SearchedSecondMainInSearch` overrides), so every deck added after the conversions — EldraziFlicker
being the live example — silently inherited greedy interior second mains and had to be
individually discovered, measured, and converted. Inverting the default ends that treadmill.

## History correction: the opt-in era was never user-approved (USER, 2026-09-05)

Do not read the old per-deck record — "antilife/hinata red, does not recover with budget",
"the adoption is per-deck", "keep greedy where searching it only dilutes the shared budget" —
as a decision the USER made or signed off on. It was not. The USER's directive to delete greedy
predates that era (2026-08-09, above), was repeated (2026-08-23), **and the USER was assured the
greedy was gone while hinata, dragonstorm, and every unconverted deck still defaulted to it at
the branch site.** The USER's words on discovering this, 2026-09-05: *"It was not deliberately
so. I had instructed other agents to delete greedy multiple times and had assurances that it was
gone."* The old hook even quoted the directive in its own comment block while defaulting to
greedy — measurement-red was treated as license to retain, and completion was reported anyway.

The standing rule this leaves behind: **a red measurement is a BUDGET problem to remedy (the
ladder below), never authorization to keep or re-introduce a greedy decision.** No future
measurement, however red, reopens that question — the remedy set is memo, depth cap, and cost
heuristics, full stop. And never report a greedy path as deleted on the strength of a default,
a doc, or another agent's assurance: the `greedysite`/`execgreedy` counters exist so "zero
greedy decisions" is a number you run, not a claim you repeat.

## The measurement this ships on (searched-design-deck-rollout.md §3c, 2026-08-26)

The d<=0 flip — the big half of this deletion, 83-100% of branch-site interior m2 calls on the
measured decks — was measured before it shipped:

* **Quality: play-IDENTICAL.** Byte-identical play digests over 2,000 unbudgeted paired games
  (AL + Kitty); of the 36 games (of 24,000) where the SHIPPED budget made the arms diverge, 35
  are digest-identical once both arms escalate budget together, and the last closes on one extra
  depth ply. There is no quality risk being traded — greedy and searched choose the same plans
  here; the deletion is about the decks and states nobody measured (*"we know it can be wrong"*).
* **Cost: a small budget dilution at shipped settings** (six cells between +0.0005 and +0.0032
  turns, all from the interior spend shrinking the outer candidate loop ~30%), **and the extra
  work is a TAIL, not a tax**: AL +0.04% total; Kitty +73.7% with 77.7% of ALL of it in two games
  (gi=231 x5.3, gi=470 x1.7).

The 2026-08-26 verdict ("NOT YET — cost alone") is superseded by the 2026-09-05 directive: the
deletion ships now, and the cost work continues on top of it. **Follow-up CLOSED 2026-09-05:
KittyEquipment gi=231 / gi=470's interior-m2 blowups are root-caused** — volume of distinct
post-combat memo keys from equipment-attachment permutations the m2 plan cannot depend on;
remedy (memo-key coarsening) designed and deferred in `kitty-interior-m2-tail.md`. The tail is
generation-time only; at shipped d5/b20 both games are unremarkable and the memo never fills.

## The shipping sweep (2026-09-05, this change, both tiers)

Smoke 68 cells: 56 identical, 6 digest-only, 6 avg-moved (worst +0.04). Regression 92 cells: 71
identical, 7 digest-only, 14 avg-moved (worst +0.03). Summed +0.08 / +0.13 turns per tier.
Per-game: smoke 13 slower / 2 faster, regression 22 slower / 7 faster -- and the classifier
(4x/16x joint escalation) decomposes that skew exactly as §3c predicted:

* **26 of 35 slower games are CHURN** (recover to the old turn under escalation): budget
  dilution, one-directional by construction (interior spend only costs at fixed budget). This is
  the whole asymmetry, and it is the class the budget ladder below exists for. Hinata pays most
  (it was never phase-split); its churn root-cause is follow-up item #1.
* **9 persist** -- all on shuffle/scry decks (hinata, fivecolour), symmetric against the 7
  faster games: tie-flip variance. The worst-luck instance is
  `hinata_regression_d3_s3003 gi111` (T8 -> loss): the OLD-vs-NEW diff shows the first
  divergence is the T1 decision itself -- old plays the tapped Mystic Monastery, new passes --
  a candidate tie at d3's horizon broken the other way, then a T4 shuffle makes the game
  physically different. Watched repro; if a pattern emerges, the candidate fix is a dominance
  tie-break ("a free land drop wins ties"), proposed and measured through the loop -- not a
  greedy revert.

Wall: regression makespan 279s -> 375s (+34%), well inside the 45-min budget. Unit 897 SUCCESS,
scenarios 72/72 unchanged.

## What a budget problem is allowed to do about it

USER, 2026-09-05: *"It's fine if we need to add heuristics to cut budget. It isn't fine to start
with something that deletes lines."*

If the searched interior m2 dilutes a deck's fixed budget, the remedies are, in order:

1. **The memo** (`SearchedSecondMainMemoized`, `MTG_SOLVE_MEMO` default-ON): each distinct
   post-combat state searched once per decision. This is what made FiveColour's conversion cost
   +0.7% wall instead of the pre-memo +9% (80% exact repeats).
2. **The depth cap** (`MTG_M2_SEARCH_DEPTH` / `MTG_M2_CAP1`): the interior m2 enumerates its full
   candidate set but scores each with one playout instead of compounding with the
   iterative-deepening pass. Still searched — no greedy pick, no line deleted.
3. Any further heuristic that cuts COST while keeping every legal line enumerated.

What is NOT a remedy, ever: reverting a deck to the greedy interior, or a gate that removes lines
from consideration. (`MTG_NO_M2_SOLVE` remains what it always was — a TEMPORARY measurement lever
for the skip-it-all upper bound, never shipped behaviour.)

### The ladder, driven on hinata (follow-up #1, 2026-09-05 — logs/hinata_m2cap)

Hinata is the deck that pays for the deletion (+0.01..+0.04 across its seven suite cells). Pooled
6-arm sweep, 900 games/arm on those exact cells (base reproduced current GT on all seven — the
apparatus check):

* **Rung 2 (the depth cap) collects NOTHING on hinata: cap1 == base on every cell, exactly.** A
  real null, not a dead lever (under the split arms the same per-job flag visibly changes play).
  Mechanism: hinata's interior spend is dominated by the ~59k d<=0 ONE-PLY searches per 50 games
  (the calls the deletion's d<=0 flip added), with only ~5k deeper calls behind them; the cap can
  only shorten the deep calls, and their full-depth answers agree with their 1-ply answers anyway.
  Corollary: for a deck whose interior profile is d<=0-heavy, rung 2 is not where the recovery is.
* **The phase-split (HINATA_ALL_MAIN2) measured RED as-is, all seven cells, both variants**
  (+0.49/game with the blanket drop, +0.33 with M2_RECONSIDER covering the drop) — but the unit
  profile (2026-09-06, logs/hinata_cost) shows the red is NOT spend: the split is **42% CHEAPER**
  (4.36M → 2.52M units) and loses anyway, because `fs_bp_wave` drops 149,753 → 0 — **FSLineTail
  has no breakpoint wave phase**, so the split moves this cantrip deck's draw-continuation
  chains into the loop that cannot walk them. Verdict recorded at the lever: the USER's doctrine
  ("we shouldn't need the first main for Hinata") is unrefuted; the m2 loop is the weaker path.
  **The lossless fix — give FSLineTail the wave walker FSLineWin has — is the top de-starvation
  candidate**: green would buy the doctrine and return ~42% of hinata's budget at once.
* Rung 1 (the memo) already runs at 47.4% on hinata vs FiveColour's 80%; with the d<=0 calls all
  keying at depth 1 and `clears=0` (the cache never fills), the misses are KEY-DISTINCTNESS, not
  the depth-fold and not capacity. Whether that distinctness is real or SPURIOUS is an open
  question this doc originally got wrong ("genuinely distinct ... no cheap key fix hiding
  there" was inference, not measurement) — the Kitty root-cause (`kitty-interior-m2-tail.md`)
  then showed exactly this signature caused by whole-state keying over detail the m2 plan cannot
  depend on. **The memo-key coarsening designed there is the candidate no-budget remedy for
  hinata's dilution too**, and measuring hinata's hit rate under it is part of that project.
* **Budget is NOT a remedy for the dilution — recorded after a wrong adoption.** The 2x/4x curve
  (logs/hinata_budget): total old-GT 5.6800 / current-1x 5.7011 / 2x 5.6689 / 4x 5.6544; held-out
  8/8 cells non-worse at 2x, −0.0156/game (logs/hinata_heldout). On that evidence hinata
  `value_play.budget_ms` 20→40 was ADOPTED 2026-09-05 — **and REVERTED 2026-09-06 on USER
  review**, because the claim "2x beats the greedy era" compared **searched@40 vs greedy@20**, an
  unfair comparator (greedy at 2x would presumably also improve — hinata is likely starved under
  ANY interior policy), and the ~2x per-game search cost was never counted against the gain. The
  USER's bar, verbatim: *"we can hardly say that is an improvement, when we didn't count the
  performance hit this inevitably brings."* Budget buys PAST the dilution; the interior spend is
  still paid inside the bigger budget. The measured curve stays useful as the price sheet — a
  starvation-motivated budget raise may still be proposed, but only with the cost counted — it
  now is: **b40 costs +39.7% units/game (43.6k → 60.9k, logs/hinata_cost) for the −0.0156 t/game
  held-out gain** — and AFTER the real dilution remedy (memo-key coarsening, above) is measured,
  so the two effects are not conflated again. The same probe's site partition generalizes the
  Kitty lead: hinata spends **49.5% of ALL units in `fs_main2`** (fs_pre 32.6%; the entire
  interior-m2 fallback family ~9%) — FullSearchLine's own second-main loop, not the interior m2,
  is where the budget goes on both decks.

  **The adoption bar this fixes in place (USER, 2026-09-06):** *"having no drawbacks in quality
  or performance would also be automatically adoptable, but this doesn't meet that bar."* A
  STRICT improvement — no quality loss, no performance loss (cf. M2_RECONSIDER's adoption) —
  adopts on its own evidence. Anything that TRADES one axis for the other is not adoptable on the
  winning axis alone: count both sides and put the trade to the USER.

### "Why is greedy so much less starved?" (USER question, 2026-09-06) — measured, and deck-split

Structurally: greedy CONSTRUCTED one m2 plan for ~zero budget; searched pays enumeration x leaf
rollout per DISTINCT memo key, from the same pool the outer deepening drinks. So the searched
interior's cost is governed by how many states count as distinct. But the magnitude splits by
deck and regime (logs/hinata_cost):

* **hinata, shipped b20: greedy was NOT much less starved.** The whole searched-interior family
  is **1.9% of units** (81.7k of 4.36M per 100 games, ~1.4 units per executed solve), and
  skipping it (`MTG_NO_M2_SOLVE=1`) makes play WORSE (+0.010/100g) — it earns its 2%. Hinata's
  starvation is `fs_main2` **49.5%** + `fs_pre` **32.6%** — FullSearchLine's own plan loops,
  identical in both eras. The deletion-era churn was a 2% budget perturbation flipping ties, not
  a large new tax.
* **Kitty, unbudgeted generation: greedy really was much less starved** — 79.6% of gi=231's
  units, from SPURIOUS key distinctness (`kitty-interior-m2-tail.md`). That regime is where the
  question's premise holds.
* **Memo capacity is nobody's problem at shipped budgets** — measured null from both directions:
  hinata b20 with 262k caps kills ALL clears (enum 20→0, solve 16→0) for +0.3% hit rate, ZERO
  unit change, digests 4/4 identical; Kitty b20 byte-identical under `MTG_BIG_SOLVE_MEMO`. The
  thrashed entries were never re-hit. Hit rates are KEY-DISTINCTNESS-limited in every memo.

De-starvation work, ranked by that evidence: (1) memo-key coarsening (the no-budget remedy;
hinata's m2 memo runs 52.9% vs healthy ~80-90%, and it unlocks Kitty generation); (2) attribute
INSIDE `fs_main2` — half of hinata's entire budget, untouched by the deletion, unowned; (3) big
memo caps only as a generation-time CPU lever (byte-identity check per deck first — generation
artifacts fingerprint play); (4) the b40 raise stays a priced trade, worth re-pricing after (1).

### The no-first-main dig (USER: "we shouldn't need the first main for Hinata", 2026-09-06)

Per-game paired evidence (100 games, seeds 4004+gi, b20, logs/hinata_cost/pergame*): the split is
**systematically** worse — 28 games lose a turn (mostly +1, one 5→unwon), 4 gain, 68 identical.
Systematic rules out tie-flip variance; the dig then EXCLUDED, by measurement, every capability
mechanism proposed so far:

* Spend (the split is 42% cheaper); full node hosting (`ROOTTURN=0`, +65% units, −0.01);
* **the m2 deferred wave phase — BUILT this session (`MTG_M2_WAVES`, heurarm-pooled, default
  OFF, byte-identical off 4/4 chunks)** and measured NULL: it fires (58k/141k units at
  `fs_m2_wave`) and moves neither base (5.6900 exact) nor split (5.99 vs 5.98). It stays in the
  tree as lossless infrastructure with its null recorded — do not rebuild it, and do not adopt
  it without a deck that measures;
* `MoveOrderPlans` (`MTG_NO_MOVE_ORDER` on the split: identical 28/4) — so the m2's candidate
  order comes from EMISSION, not the sort;
* the m2 land drop (split+blanket-drop recovers only ~0.06 of the ~0.29).

What the game diffs actually show (three exemplars, logs/hinata_dig):

1. **gi=0 (5→6): a rank-7 PEER tie broken differently.** T1, identical hands, both cantrips
   score sub=6 with empty continuations (M2T/FSW traces): m1's emission order casts Ponder,
   m2's casts Preordain, and first-verified-win keeps each. The peers are tied BY DESIGN
   (the interleave-reachability ruling), so neither loop is "wrong" — they just disagree, and
   m1's accident plays better.
2. **gi=88 (5→UNWON): same casts, different RESOLUTION.** Base and split cast the identical
   Ponder/Preordain/Ponder sequence T1–T3, yet the T4 draws diverge — **the Ponder top-3
   reorder resolved differently inside the m2 apply than the m1 apply**. The scripted-choice
   chooser is phase-context-sensitive, and the m2 answer is worse.
3. **gi=22 (4→5): the Soulfire reveal line goes missing in m2.** Base casts Soulfire Eruption
   pre-combat T4 for the kill; the split's T4 m2, with the same mana, never produces the line.

**ROOT CAUSE FOUND (2026-09-06, the trace dig): the post-dedup searched sub-decision AXES are
m1-HOST-ONLY.** A temporary trace in `HeuristicTopDisposition` on gi=88's T3 Ponder showed the
m1 arm exploring `ponder_keep = 0/1` pinned variants (the searched keep-vs-shuffle; it chose
SHUFFLE and won T5) while the m2 arm ran `keep_decision = -1` — the resolution heuristic — on
every single arrival (it kept, and never won). The emitter is the post-dedup axis fan-out in
`EnumeratePlansWithLand` (`MTG_PONDER_AXIS` default-ON at src/ai/TurnSolver.cpp:27481, plus the
tutor / etb-dig / scry / lackey axes appended in the same host); `EnumeratePlansM2Memoized`'s
own host-namespace comment states its body is plain `EnumeratePlans` — "no land axis / no
appended breakpoint variants" — and NO axis fan-out. So every searched sub-decision the
2026-08-01 "post-dedup axis" architecture created exists ONLY in the pre-combat main.

Consequences, in order of importance:

1. **This is a live asymmetry in BASE play, not just the split**: any Ponder / tutor / dig /
   scry cast in a second main — which base play does routinely — resolves by heuristic while
   the identical cast in main 1 is searched. It is exactly the "searched at every level"
   directive class, phase-scoped instead of deck-scoped.
2. It explains the split's systematic loss (the split routes ~every cantrip through the
   axis-less host) and why every breadth mechanism measured null: the missing lines were never
   in the plan list to begin with — no wave, node, or ordering change can score a variant that
   is not emitted.
3. **The fix is the lossless class**: append the same post-dedup axis fan-out in the m2
   enumeration hosts (FSLineTail's `EnumeratePlansM2Memoized` / `EnumeratePlansWithLand(m2)`
   route, and the interior-m2 enumeration if it is likewise bare). Cost multiplies m2 plans, so
   it ships through the usual measurement loop — but unlike the waves, this one targets the
   measured mechanism. Re-measure `HINATA_ALL_MAIN2` only after it lands.

### The fix, BUILT and MEASURED (2026-09-06, same day): `MTG_M2_AXES`

The whole 670-line axis block was factored VERBATIM out of `EnumeratePlansWithLandUncached`'s
tail into `AppendSubdecisionAxes(state, is_pre_combat, all)` — the m1 caller calls it at the
same point, and 200/200 base+split per-game digests match the pre-refactor binary, so the
refactor is byte-identical. The two bare m2 hosts now call it behind `MTG_M2_AXES` (DEFAULT
OFF, heurarm slot `M2_AXES` for per-job pooling):

* `EnumeratePlansM2Memoized`, via a shared `EnumerateM2PlansBody` used at ALL THREE of its call
  points (memo-off fallback, verify recompute, cache miss) — one body, so the memo verify can
  never diverge from the cached list merely because the lever is on;
* the no-drop EARLY RETURN in `EnumeratePlansWithLandUncached` — which is where the interior-m2
  route (`SearchedSecondMainMemoized` → `SolveWithLookahead(m2)`) was going bare: it arrives at
  m2 with the drop consumed and used to return before the fan-out. m1 no-drop re-solves stay
  bare (outside the measured defect); both new call sites gate on `g_bp_enum_depth == 0`, so a
  breakpoint continuation list never fans (BpEnumEntryFor's rule).

Known inert-duplicate: `Plan::dig_choice` is consumed on the `is_pre_combat` apply path only,
so the cycle/sac-draw dig axis's m2 variants tie-break away (Auras is the only opt-in). The
condemned-tranche enumeration (FSLineTail's rescue path) deliberately stays bare.

**Measured (hinata, 100 per-game train games at b20, one pooled 400-job batch;
`logs/hinata_m2axes/`):**

| arm | avg | vs base | pairwise | units/100g | vs base |
|---|---|---|---|---|---|
| base | 5.6900 | — | — | 4.36M | — |
| base + M2_AXES | 5.7000 | +0.0100 | 1 better / 2 worse / 97 tie | 4.61M | **+5.9%** |
| split (`HINATA_ALL_MAIN2`) | 5.9800 | +0.2900 | 4 / 28 / 68 | 2.52M | −42% |
| split + M2_AXES | 5.8400 | +0.1500 | 4 / 18 / 78 | 3.06M | **−30%** |

* **The mechanism is confirmed**: gi=88 (the traced Ponder keep-vs-shuffle game) recovers 9→5
  under split+axes — the base win turn exactly — and gi=0 recovers 6→5. split+axes vs split is
  15 better / 6 worse, −0.14/game: **the axes buy back half the split's red** for +21% of the
  split's units, landing still 30% cheaper than base. The added spend sits where it should:
  `fs_main2` 1.12M → 1.66M (+48%), the axis variants being scored.
* **The default stays OFF**: on base classification the axes are +5.9% units for +0.01 (noise,
  97 ties) — not a strict improvement, so flipping the default is a trade that belongs to the
  user. On base play the key cantrips route through the already-searched m1 host, so the m2
  axes rarely bind.
* **The split doctrine is half-rescued, not settled**: 18 games still lose vs base under
  split+axes (gi=22's missing Soulfire-class line among them — a different mechanism from the
  axis bareness). The residual is the next dig if the all-main2 doctrine is to go green.

### The residual mechanism, characterized (2026-09-06, same day): the split forfeits the FREE INTER-MAIN RE-SOLVE

Game-log diffs on three residual reds (gi=22/51/66, `logs/hinata_m2axes/g*_{base,splitax}`) all
show one structural motif. gi=66 T5 is the clean exemplar: BOTH arms cast the identical
Preordain → Gamble → Reality Spasm (untap) → Expressive Iteration chain, and EI draws Crackle
with Power mid-chain, with the opponent at 8.

* **base** runs the chain in m1, then gets a **fresh full-width m2 solve** on the post-chain
  state — which sees the drawn Crackle and casts it for the T5 kill. The m1→m2 phase boundary
  is acting as a free extra search node: a whole second decision at deck width, at zero
  marginal enumeration cost, every turn.
* **splitax** runs the same chain as its ONE committed m2 plan; the drawn Crackle can only be
  reached by a breakpoint continuation of that plan, and no m2 mechanism reaches a TRAILING
  continuation. The turn ends with **six untapped sources and lethal in hand** (log-verified),
  and the win slips to T6.

What it is NOT, each excluded by measurement on gi=66:

* Not budget: b40/b80/b160 all still lose the turn (win T6 at every budget).
* Not the wave walker: `MTG_M2_WAVES=1` (with axes, both budgets) unchanged.
* Not an executor divergence: `MTG_M2_YIELD_STATS` shows breakpoint-fallback=0,
  searched-resolve=0, every nohost is `[rollout+rec]` — the SEARCH never scored the T5 line,
  so the executor faithfully played the committed T6 line.
* Not the sub-decision axes (they are what `MTG_M2_AXES` just fixed): the missing cast is a
  freshly-DRAWN card, which no pre-draw plan subset can contain.

A second m1-only capability surfaced in the same dig: `AppendBreakpointVariants` is also absent
from `EnumerateM2PlansBody` — m2 plans carry no `bp_choice` variants at all, so even NON-trailing
continuations of an m2 plan are searched only where BP_NODE hosting covers them.

**Fix sketch (not built):** give the second main a FIXPOINT RE-SOLVE — after an m2 plan that
fired a draw-breakpoint resolves, re-solve m2 on the resulting state (loop until the solve
returns no action). That is legal MTG (still the same main phase), and it is exactly the second
decision base play gets from the phase boundary for free. It must land on BOTH sides in
lockstep: the search's m2 scoring (FSLineTail's loop currently goes plan →
SimulateEndAndStartNextTurn directly) and the executor's m2 execution, else realized and scored
turns diverge. gi=22 additionally shows the phase-ORDER half (Soulfire cast m1 gets two phases
plus a combat for its revealed free casts; m2 gets one), which no re-solve can restore — that
part is inherent to the classification and bounds how green the doctrine can ever go.

### BUILT (2026-09-06, "build any and everything" directive): `MTG_M2_FIXPOINT`, kill-scan

THREE designs were built and measured the same day; the first two are the cautionary half of
the record — each rejected by a different instrument, which is why both instruments exist.

* **v1 — Unconditional re-solve (REJECTED by the per-game battery).** The literal fix
  sketch — after a breakpoint-firing m2 plan, re-solve and play whatever the fresh solve
  says, on all three hosts (FSLineTail recursion, the interior `SolveSecondMainInSearch`
  apply sites, executor re-entry). It recovered gi=66 (9→5 class) but measured NET-RED on
  the 100-game battery: split+axes+fix vs split+axes 6 better / 10 worse (+0.08/game).
  Mechanism: the executor's extra NON-LETHAL casts realize a future the outer scored line
  never priced — playing more cards is not free when the next turn's solve wanted them.
* **v2 — Lethal-only via NESTED SOLVE (REJECTED by the suite A/B).** Only commit the
  re-solve's line if it kills. Quality clean on the battery (6/0), but the smoke suite
  showed **478 games slower** and a hinata d0 cell flipped a win to a LOSS: (a) a full
  `SolveSecondMainInSearch` per draw-firing rollout m2 is a budget tax paid by every deck on
  every rollout; (b) the executor's lethal gate sat on the searched branch that d0 play
  never reaches, so d0 re-entries played ungated extra casts (the v1 harm, resurrected on
  one depth). One-deck train evidence cannot see either failure — the suite can. (Caveat
  added after the misattribution below was found: v2's suite-red magnitude is confounded by
  the same MainPhase loop bug if it was in the tree for that run; the d0 flip and the
  per-rollout nested-solve tax are the load-bearing rejections, and they stand on their own
  mechanisms.)
* **v3 — KILL-SCAN, projection-filtered (CURRENT, default OFF).** No nested solve anywhere.
  At each host, ENUMERATE the post-draw m2 plans, probe-apply ONLY those whose enumeration
  entry projects `wins_this_turn`, and commit solely on a verified `OpponentHasLost` —
  otherwise change nothing. `wins_this_turn` is the OPTIMISTIC lethal projection, so it
  over-counts and every real kill passes the filter. Cost of the filter: 2 of the 6
  split-arm rescues have kills whose projection does not fire; they stay stranded. All
  three hosts apply the same rule: the FSLineTail m2 loop (gated on `g_bp_fired_last`,
  adopts a consecutive-m2 two-`PhasePlan` win; the executor replays it via the
  `HasCommittedPhasePlan` loop in `GameEngine::MainPhase`), the interior
  `ApplySecondMainInSearch` helper (wraps all five interior apply sites), and the executor's
  `AIEngine::TrySecondMainStrandedKill` (probes under `RevealLogPause`, real
  `TurnSolver::ApplyPlan` only on the kill). Executor draw signal = `cards_drawn_this_turn`
  delta stamped by an exit guard on `TakeTurn` (`m_m2_exec_drew`), since a non-committed
  plan's cantrip draws never touch the breakpoint machinery — the first gate attempt,
  `g_bp_seen_last`, is only written for plans carrying a `bp_choice` and read 0 on the exact
  plan the lever was built for. The re-entry is depth-independent (no flag-threading through
  the search), which is what fixes v2's d0 hole.

**The suite-red MISATTRIBUTION (2026-09-06, caught by an off-arm control).** Every suite A/B
of this lever showed combo decks devastated (dragonstorm +1.8/game, breaching +1.5), and it
was twice attributed to the scan (first "probe everything", then "the filter should fix it").
Both attributions were WRONG: the filtered and unfiltered binaries produced BYTE-IDENTICAL
red suites (net +1120.0 both times), and a single-process control with the LEVER OFF
reproduced dragonstorm's 6.30 exactly. The regression was a lever-independent bug in the new
`GameEngine::MainPhase` re-entry loop: `HasCommittedPhasePlan` matched on PHASE alone, but a
committed verified-win `SearchLine` spans MULTIPLE turns — for an m1-only deck the deque
holds adjacent pre-combat entries (`[T4 m1, T5 m1, ...]`), so the ungated loop replayed the
whole committed line in one main phase. Storm decks commit verified lines constantly, hence
the deck pattern (dragonstorm/breaching worst, th/mirrorwing next, hinata d0 untouched — d0
never commits). Fixed by gating the loop on the LEVER and on `!is_pre_combat` (consecutive
m2 entries can only be a same-turn fixpoint pair, because every turn's phases begin with a
pre-combat entry). File-level bisect against HEAD found it; the off-arm control is the
lesson — a red A/B where BOTH arms are red is not measuring the lever. Post-fix: dragonstorm
d3 sits at GT (4.5533) with the lever off AND on; the g16 executor rescue survives (6→5).
Consequence: the projection filter's original justification is void — whether the
UNFILTERED scan (which rescues 2 more split games) is affordable is an open A/B, to be run
with the loop fixed.

**Measured (100 per-game train games, `logs/hinata_m2axes/pergame6d.out`, cost 4×25 in
`cost_*.err`):**

| arm | avg | pairwise vs its base | units |
|---|---|---|---|
| base | 5.6900 | — | 4.36M |
| base + FIX | 5.6800 | **1 better / 0 worse / 99 tie** (g16: 6→5) | −0.8% |
| split+axes | 5.8400 | — | 3.06M |
| split+axes+FIX | 5.8000 | **4 better / 0 worse / 96 tie** (g19 8→7, g57 7→6, g66 6→5, g79 6→5) | ~flat |
| split+axes+FIX+BPVARS | 5.8000 | 0 better / 0 worse vs +FIX (byte-tie) | — |

* gi=66 recovers its base win turn in the split arm; gi=16 recovers a turn in BASE play (base
  m2 plans strand kills too — the lever is not split-specific). base+FIX's avg 5.68 equals
  the pre-deletion greedy-era GT number: on train evidence the hinata deletion regression is
  recovered.
* The split's gap vs base is now **+0.11/game** (from +0.29 bare, +0.15 with axes) at −30%
  units. The residual worse games are the phase-order class plus the 2 projection-filtered
  kills.
* `MTG_M2_BPVARS` measured inert-to-slightly-negative on this battery; stays OFF, kept as a
  recorded lever.
**The cross-deck verdict (post-loop-fix smoke A/B + pergame7 battery, 2026-09-06):**

* Smoke, `MTG_M2_FIXPOINT=1` vs GT: **2 of 72 cells changed, BOTH improvements** — hinata
  d0 −0.0120/1000g (the executor scan firing at depth 0, where no search-side rescue can
  exist) and melira2hg d3 −0.0400/25g. Net −13 game-turns, zero cells worse; combo decks
  sit exactly at GT.
* pergame7 (fixed code, 6 arms × 100): fix vs base **1/0/99** (g16), unfiltered ≡ filtered
  on base play (0/0/100). Split arms: safix vs sa 4/0/96; **safixunf vs sa 6/0/94** — the
  unfiltered scan recovers both filtered-out rescues (g5, g16), split gap vs base +0.09.
* Cost: hinata 25-game units base 1.119M → fix 1.069M (**−4.5%**, rescued games end
  sooner); fixunf 1.073M units but **+15% wall** on base play in the pooled battery (probe
  copies are wall the unit counter barely sees) for zero base-play quality — so UNFILTERED
  stays a split-doctrine lever only. Cross-deck wall, per-cell sum of the off/on smoke
  runs: **+5.25%** with the lever on (67 of 72 cells play byte-identical games, so most of
  that is pure scan overhead; the concentrated cells: melira2hg +19% — the cell that also
  improved — burn d3 +30% on a 26s cell, hinata d3 +15%).

**Default: OFF, by the adoption bar.** Quality is strictly non-worse everywhere measured
and better in three places, but +5.25% suite wall makes this a quality-vs-perf TRADE, which
is the USER's call, not an auto-adoption (strict improvement requires no perf drawback).
Both sides are counted above. A per-deck middle path exists: for hinata the lever is a
strict improvement ON ITS OWN TERMS (quality better AND units cheaper), so a provider-level
opt-in would adopt it where it pays without taxing the suite. (The wall figure needs a
re-measure on a quiet box -- another container shared the machine during these runs; the
per-cell ms comparison is scheduling-contaminated, proven by ±13% swings on byte-identical
cells. Quality/units/digest verdicts are deterministic and unaffected.)

### MODE 2 (USER direction 2026-09-06): the gated FULL RE-SOLVE — "we need to re-evaluate"

The USER's ruling on the kill-only design: *"I don't see how we can have a correct solution
without re-evaluating at those points. We can use some of the techniques discussed before
like condemnation to reduce what we re-evaluate, but we need to re-evaluate."* Mode 2
(`MTG_M2_FIXPOINT=2`, heurarm `M2_FIX_RESOLVE`) is that design: a REAL re-solve at the
post-draw points, priced by **new-information condemnation** — the pre-draw solve already
adjudicated the old hand, so the re-solve is condemned unless a card DRAWN during the plan's
execution is ACTIONABLE (appears in some enumerated post-draw plan; `M2FixDrawnDelta` /
`M2FixActionable`, hand-multiset deltas over the apply). Kill-scan stays as the always-on
floor beneath it.

* **Scored line**: `FSLineTail`'s fixpoint block RE-ENTERS ITSELF on the post-draw state.
  The recursion enumerates the post-draw plans INCLUDING the empty plan — whose tail is
  exactly the plain EOT tail — so when the gate fires it REPLACES the plain tail: a strict
  generalization, no double scoring. Continuation phases land in the returned line as
  consecutive m2 `PhasePlan`s; the executor replays them via the `MainPhase` loop.
* **Interior/rollouts**: `ApplySecondMainInSearch` adds a gated `SolveSecondMainInSearch`
  re-solve after a no-kill scan (nest-capped).
* **Executor**: `TrySecondMainStrandedKill` falls through to a full `SolveWithLookahead`
  re-solve at deck settings — **NON-COMMITTED play only**. The first build fired it on
  committed lines too and re-learned v1's lesson in one game: the re-solve cast cards the
  committed line had allocated to FUTURE turns, the stale line then replayed into nothing
  (hinata g63 split, 7→8, bare attacks from T6). A live committed line was scored WITH the
  mode-2 recursion — its post-draw continuations are already IN the line — so an executor
  re-solve there is redundant and line-invalidating. Gated on `m_committed_line.empty()`;
  g63 recovered to mode-1 parity.

**Measured (pergame8/8b, hinata train, 100 games/arm):** base doctrine: mode 2 ≡ mode 1
EXACTLY (0/0/100; both 1/0/99 vs base; units +0.3% — the condemnation gate holds). Split
doctrine: **safixr vs sa 6/1/93** — the six rescues include g5 and g50, the two kills whose
projection never fires (mode 1's filter can't see them); the one loss is g14 (7→9), a
pre-verification GRADING flip: at T1 a turn-7 win is beyond the horizon (1+depth5=6<7), so
no verified win existed and mode 2's re-scored interior legitimately shuffled the graded
pick — honest variance, not unsoundness (no [nonconv] event; persists at 4x budget, so not
dilution either). Split avg ties mode 1 at 5.80.

**ADOPTED (2026-09-06): hinata provider opt-in, mode 2** (`HinataProvider::M2FixpointOptIn`
= 2; `M2FixModeFor` resolver in DecisionProviders.h; `MTG_M2_FIXPOINT=0` hard-disables for
A/B). The USER approved the per-deck opt-in and challenged d0-weighted evidence; the
DECIDING number is the SHIPPED config (d5/b20), 1000 distinct paired games: **5 better /
1 worse / 994 tie** (5.7060→5.7020). The 18-deck sweep found hinata the only converter —
15 decks never fire the class, burn and kitty fire it thousands of times and convert
nothing (the clearest opt-OUTs: they would pay the most CPU for zero gain). GT accepted
for hinata's cells both tiers; every other deck byte-identical. Global default stays OFF.

**HELD-OUT (regression tier, disjoint seeds s2002/s3003, melira excluded per USER):** both
modes net POSITIVE and all movement is hinata's — mode 1: −5 game-turns (d0 −0.003, d3
s3003 −0.01, d5 s3003 −0.01, d5 s2002 +0.01 = ONE game the grading-variance way); mode 2:
**−12 game-turns** (same cells, d0 −0.010/1000g). Every other deck sits exactly at GT on
both tiers. Combined with the train evidence and the quiet-box CPU table (hinata ~+8%,
burn ~+5%, stompy ~+2%, ~0 elsewhere; mode 2 = mode 1 + 1–2 points), the package for the
USER's default/opt-in decision is complete: quality is net-better on train AND held-out in
both modes (mode 2 strictly ahead of mode 1 on both), the cost is a few percent CPU
concentrated in draw-heavy decks, and the one residual harm class is pre-verification
grading variance (~1 game/100 on the affected deck).

## What stays a provider decision

Per the USER (2026-09-05): skipping a main is acceptable only as an explicit opt-in, and that is a
provider decision — deck-specific judgment adopted through the standard loop (propose variants,
measure on the harness, report, adopt in the archetype provider on the USER's approval). Two hooks
remain on that surface, both defaulting conservative:

* `SkipsUnproductiveSecondMain()` (default false): opt-in to `SecondMainUnproductive`'s
  skip-the-solve gate.
* `SearchesRolloutSecondMain()` (default false = greedy leaf estimator): the ROLLOUT site's
  playout policy. Greedy here is by DESIGN, not by neglect — the leaf estimator is a scoring
  device, not a decision ("OPTIMISTIC where you BRANCH, HONEST where you SCORE"), and
  Anti-Lifegain measured searching it at +12 turns/3000 games (d3) with the cost NON-MONOTONE
  (greedy is an interior optimum). The USER's ruling stands: rollouts being greedy is fine.
  Structural guard preserved: the rollout site at `depth <= 0` stays greedy even for an opted-in
  deck — rescuing it would recurse without a decrementing bound.

## What was deleted

* `DecisionProvider::SearchedSecondMainInSearch()` and every override (KittyEquipment's
  unconditional `true`; Anti-Lifegain's `MTG_AL_SSM`-gated one; FiveColour's `MTG_5C_SSM`-gated
  one). Their measured evidence is what justifies the flip: Kitty four arms byte-identical
  (digest 3e6ea44e9c15d572), AL branch site byte-identical over 26,000 games, 5C digest-only at
  identical averages.
* `GreedySecondMainEnabled()` / `g_search_second_main`, and the levers `MTG_SEARCH_SECOND_MAIN`,
  `MTG_NO_SEARCH_SECOND_MAIN`, `MTG_SSM_SITE`, `MTG_M2_D0_SEARCHED` (+ heurarm slots
  `NO_SEARCH_SECOND_MAIN`, `AL_SSM`, `SSM_BRANCH_ONLY`, `M2_D0_SEARCHED`). Kept: `AL_SSM_ROLLOUT`
  (rollout-site policy), `M2_CAP1` / `MTG_M2_SEARCH_DEPTH` (budget levers).
* `AntiLifegainProvider::SearchesRolloutSecondMain` keeps its decline (now
  `heurarm::Flag(AL_SSM_ROLLOUT) && AlPhaseEnabled()`), no longer chained through the deleted
  branch hook.

## Stale after this change (listed, deliberately not rewritten)

* `test/tools/kitty_ab/gen_manifest.py`, `gen_escalate_manifest.py`, `gen_m2d0_manifest.py` —
  concluded-A/B archives that emit now-deleted levers; they document past method and will not run
  against this engine.
* The historical narrative in `second-main-greedy.md`, `searched-second-main-adoptability.md`,
  `antilife-main-phase-split.md`, `analysis-KittyEquipment.md` — history docs; superseded on
  policy by THIS file. `greedy-in-the-searched-window-status.md` and
  `searched-design-deck-rollout.md` §3b updated to point here.

## The EXECUTOR half (2026-09-05, later the same day — found by another agent's report)

The census tables above cover the search interior; the last greedy DECISION was not in the
search at all. When a COMMITTED plan reached a breakpoint occurrence carrying no searched
continuation (`bp_choice < 0` — a base plan that won the turn and then hit a breakpoint no
variant targeted), the executor ran a greedy `Solve()` for the rest of the turn — real play,
real decision, greedy. Deck-shape dependent: 8 of 50 Melira games, 0 of 50 hinata (which is why
a hinata-only census missed it).

**Fixed the same day:** both executor fallback sites (the main breakpoint applier and the pod
trailing-pass twin) now run a full SEARCHED re-solve at deck settings (`SolveWithLookahead`,
deck depth/budget, shared TT). Verified on Melira: `breakpoint-fallback=0 (base=8 ...
searched-resolve=8)`. The rollout twins keep greedy (playout scoring, the tolerated scope), so
realized-vs-scored can diverge on these continuations — in the conservative direction only (the
realized continuation comes from a strictly stronger solver than the one that scored it).
`MTG_EXEC_BP_SEARCHED=0` restores the greedy. The census's `why_kind` table (added with this
change) attributes every unresolved continuation class by ROOT/REC/RESUME apply kind, so
"zero greedy decisions" is a measured number for any future deck, not an assertion from the
nohost table alone.

## The ONE sanctioned greedy construction: a verified this-turn combo go-off

USER, 2026-09-05: *"The only exception I can think of is perhaps that we might want a greedy combo
go-off implementation for complex combos like EDF. However, those are strictly acceptable when
they win this turn."* -- *"So, if they fail to do so, they would fall back to search."*

That is the go-off short-circuit family (Dragonstorm's storm line; EDF's blink line), and its
invariant is exactly the user's boundary:

1. The combo line may be GREEDILY CONSTRUCTED (the recognizer's arithmetic, not a search).
2. It is TAKEN only on a VERIFIED this-turn win: re-simulate via ApplyPlanDirect and
   short-circuit only when OpponentHasLost holds on the resulting state -- never on the
   projection alone (the EDF Gorge repro is why: the colour-blind projection claimed a k=16
   lethal the apply realises as 4 damage).
3. On a non-win it RESTORES best/best_mask byte-identically and falls through to the full
   search; the greedy line's score is discarded, so no greedy judgment leaks into ranking on
   building turns.

A this-turn win is the objective's minimum, so everything the short-circuit skips is dominated --
the greedy construction never decides anything that is not provably optimal.

## What remains greedy inside the search (measured, not assumed)

The `greedysite` counters (`MTG_M2_YIELD_STATS`) count every remaining greedy `TurnSolver::Solve()`
reached from inside the search. Census on THIS binary (hinata, the bp-heaviest deck, 50 games):

```
M2 SITE:  BRANCH searched 4988 / greedy 0 / d<=0 59151 (all SEARCHED)
EXECUTOR: greedy Solve breakpoint-fallback=0 | REAL main-phase decisions: NONE
bp-continuation nohost by apply kind: [rollout] + [rollout+rec] = 100%  (no ROOT kind)
```

**The searched window is greedy-free.** Every residual greedy fire is PLAYOUT-side: the rollout
leaf estimator (above), `SolveWithLookahead`'s `depth<=0` base case (site 90 -- the playout policy
itself), and the breakpoint-continuation fallbacks inside plain rollout applies -- whose
DECISION-side half was already deleted by the 2026-09-02 TIGHT sound recipe
(`bp-greedy-continuation-deletion.md`; canon covers root enum, node resume, captured applies).
The playout scope is the USER's own ruling, twice: rollouts may stay greedy because budget
recovers playout deficiencies, and only the searched structure cannot be budget-recovered.
Re-opening the playout side = `MTG_BP_CANON_REC` (measured -0.007t hinata for +12% wall) or,
cheaper, the incremental-key project.
