# Anti-Lifegain: the enforced main-1/main-2 split (MTG_AL_PHASE) — built, measured, DECISION OPEN

**Status 2026-08-21:** all levers BUILT and DEFAULT OFF (off-state byte-identical, per-deck
regression 5/5 PASS). Train is green; **held-out is slightly red with a named residual class**,
so the USER's adoption condition ("if my preference works better, let's stick with it") is not
yet met. The decision — adopt at the measured cost, iterate the classification, or stay with
re-evaluation — is the USER's. Everything below is measured on the engine at/after commit
a16074a (the payment-rollback fix this arc surfaced).

## Context

USER ruling (2026-08-21): *"There should be a real split between the two, not a re-evaluation"*
— the m1/m2 classification should be ENFORCED (Main2-classified casts withheld from the
pre-combat enumeration), not labels-plus-leftover-re-offer. Anti-Lifegain is the only
approved-order deck besides FiveColour with a second main at all (`uses_second_main=yes` via
`lifegain_to_loss`); CG/Knights/MW/slivers are single-main decks (verified byte-identical under
`MTG_SEARCH_SECOND_MAIN=1`, 20/20 keys — no interior m2 exists for them).

## The levers (all in AntiLifegainProvider / GoldFishRunner)

| lever | default | what it does |
|---|---|---|
| `MTG_AL_PHASE` | OFF | opt AL into the pre-combat Main2 filter (`ClassifiesMainPhases`), the 5C-shape per-deck adoption |
| `MTG_AL_DORK_M1` | ON (within the split) | mana dorks stay Main1 (the base sick-body rule sent Birds m2; measured below) |
| `MTG_AL_PHASE_ROOT` | OFF (**measured rejection**) | root-turn authority for the filter (condemnation-arc shape) |
| `MTG_AL_SSM` | OFF | searched interior second main, scoped to the split being live |
| `MTG_AL_SINGLE_MAIN` | OFF (**measured rejection**) | drop the deck's second main entirely |
| `MTG_M2_SEARCH_DEPTH` | unset (inert) | cap the interior m2 solve depth; measured NEVER-BINDS at gate budgets |

## The classification under the split (base rules + overrides)

m1: Tainted Remedy, Plague Drone, Ignoble Hierarch (`MTG_AL_DORK_M1`: Birds too), Aria of
Flame, both tutors, Invigorate, Swords to Plowshares.
m2: Birds of Paradise (without DORK_M1), Fiery Justice, Skyshroud Cutter, Reverent Silence
(USER 2026-08-18 override). Goldfish never blocks, so pure-damage m2 is safe (no
blocker-removal value pre-combat).

## Measurements (all per-game vs committed GT / fixed control)

**Train (per-deck regression, seeds 2002/3003):**
* PHASE: net −0.0033, 3 games faster, 1 churn-worse (recovers 4×), d0 byte-identical,
  ~880 searched games digest-only at identical scores.
* PHASE+SSM: +0.0107 — the SSM churn tax returns (all recover 4–16×; one fetch-draw-divergence
  game). SSM alone verified inert (scoping works).
* SINGLE_MAIN: red every key incl. d0 (+0.018..+0.028).

**Held-out (per-deck overnight, seeds 4004–10010, 8 searched keys / 8000 games):**
* PHASE (filter-everywhere): **+22 turns net, 53 worse : 29 faster**; escalation battery (both
  arms, 4×/16×): 13 churn, **38 budget-flat persistent** (4/4/4 vs 5/5/5 — the filtered-line
  signature), mostly +1-turn slips on fast T3/T4 kills, identical at d3 and d5.
* PHASE+DORK_M1: **+20 net, 37:16** — the dork rule recovers the Birds subclass (gi8-class:
  T2 "dump Remedy+Invigorate now" over "cast Birds, hold for the T3 kill") but trades away
  faster-games nearly 1:1.
* PHASE+ROOT (root-turn authority): battery recovers 39/53 of the old class but the full
  held-out **collapses to +83 net, 82:2** — filtered root candidates scored by unfiltered
  future projections is unsound mixed semantics. NOT cache poisoning (memos-off battery
  identical). Default flipped OFF, kept as instrument.
* SINGLE_MAIN vs fixed control: still +0.016..+0.028/key, 47:5 — the deck's "attack into
  range, then payload post-combat" lines degrade when everything must commit pre-combat.
  The USER's mechanical point is CONFIRMED though: nothing is rules-prevented (traces show the
  m1 unload reaching the same totals); the losses are the engine's pre-combat gates/attack
  decisions — a heuristic class, budget-immune, in principle fixable with attack-projection
  credit in the payload gates, unfunded because the split beats it anyway.

**The residual class (PHASE arms):** hold-vs-dump misvaluation. Diverging turns pick an early
Remedy/Invigorate dump over the control's hold-for-the-big-turn line even when the winning line
is fully expressible under the split (verified per-game: gi244's T4 Aria(+10-flip)+Invigorate
kill is legal in both arms). Zero nonconvergence (MTG_FD_ORACLE clean) — the search honestly
prefers the worse line. Mechanism: the filter runs inside rollout projections, and the greedy
playout tail plays deferred casts badly, deflating hold-lines. Budget-, depth-, SSM- and
dork-classification-immune; root-authority (the only precedented repair) measured worse.

## What this arc also surfaced (committed separately)

The USER's off-by-one audit of a T3 main found the **payment-fallback opponent-life rollback
bug** (fixed + GT rebaselined at a16074a): PayCost's greedy-fail fallback restored
battlefield/own-life/graveyard but not opponent life, so a Grove drip paid in the failed greedy
arrangement fired AGAIN under the backtracker — one cast, two drips.

## Open decisions (USER)

1. Adopt PHASE(+DORK_M1) at ~+0.0025/searched-game held-out cost for the doctrine preference —
   or keep re-evaluation (status quo) — or fund the residual-class dig further (next untried
   angle: value-model treatment of the playout tail, i.e. value-leaf territory; or per-card
   classification shrink — each m2→m1 move shrinks the filter toward re-evaluation).
2. MTG_AL_SSM: greedy interior m2 measures BETTER than searched for AL either way; the
   doctrine-complete form costs the churn tax. (5C's financing — the enum memo — does not bite
   here: 11 hits vs 1802 misses on a churn game; m2-search-memo 27%.)
3. Delete the measured rejections (`MTG_AL_SINGLE_MAIN`, `MTG_AL_PHASE_ROOT`,
   `MTG_M2_SEARCH_DEPTH`) or keep as instruments.

## Repro notes

Single-game repro needs BOTH `seed = base+gi` AND `game_index = gi` in the batch manifest —
the opponent-spawn schedule keys on `gi % 10`; a wrong game_index reproduces a different game
silently. Arm runs + escalation batteries + traces under `logs/ssm_sweep/` (gitignored).

## 2026-08-21b: THE WHY DIG (USER: "lossless option with ordering, condemnation and 0 greedy --
## make it work") -- causal chains named per-game; both repair levers measured-rejected by decouple

USER rulings this session: the design stands absent a "really strong reason"; fully STATIC m1/m2
labels are likely flawed (conditional rules expected, as 5C's are).

**The full causal chain of the flagship game (overnight d3_s5005 gi244), five layers deep:**
1. T1 near-tie land order (Heath vs Flats) flips under filtered projection values.
2. The T2 fetch pick decides the game: Stomping Ground (the deck's only R among fetchables,
   needed by held Aria of Flame) vs Temple Garden. The fetch rank counts battlefield dork
   colours as coverage, so Hierarch "covers" R -- but spending its R forfeits the attack+pump
   kill (an attack-forfeit-ledger instance inside the fetch rank).
3. Hold-vs-dump at T2 rides on the tail's post-fetch SHUFFLED draw stream -- fetch-decision
   clairvoyance, the decouple-arc class: each plan prefix shuffles differently, so hold lines
   are valued against different projected draws per arm. Scenario fixtures (new `library_top`
   support) prove BOTH arms find the hold-kill from pinned T2/T3 states at b0 -- the machinery
   is complete; the in-game miss is projection draw-luck plus the rank flaw above.
4. The greedy tail executes the kill turn CORRECTLY when an R land is on board (scenario
   `al_hold_kill_turn.json` PASSes stock) and cannot without one -- the earlier "greedy can't
   execute" reading was the R-starved variant.
5. The Aria "off-by-one" was the card working as printed (verse damage is IMMEDIATE per cast --
   Scryfall-verified 5-life gift); the only true accounting bug found was the payment-rollback
   opponent-life leak (fixed a16074a).

**Repair levers built and measured (decouple ensemble, salts 1-4 x 8 keys x 1000 games):**
* `MTG_AL_FETCH_ATK` (sole-attacker dork colours are not fetch coverage): fixes gi244's pick,
  but +0.0008..+0.0018/game WORSE than stock on every salt -- REJECTED (instrument only).
* `MTG_AL_RED_ENABLER` (redundant live-enabler copy demoted below payoffs): digit-identical
  aggregate on every salt -- INERT (fires too rarely).
* Reference decoupled costs: PHASE+DORK_M1 vs stock = +0.0034..+0.0048/game (all salts);
  PHASE+both-repairs = +0.0050..+0.0071 (worse). Root-turn authority: see above (rejected).

**Where the lossless mandate stands:** every named mechanism is now either fixed (payment leak),
rejected-by-measurement (fetch-atk, red-enabler, root authority, single-main), or identified as
fetch-shuffle projection clairvoyance -- which the COUPLED metric scores but a decoupled one
discounts, and which affects the two arms asymmetrically only because their prefixes shuffle
differently. The split's decoupled cost (~+0.004/game) is the honest open gap. Candidate next
moves (unfunded): (a) decouple-aware projection for fetch-class decisions inside the tail (score
hold/dump under a salted ensemble rather than the real stream -- heavy); (b) conditional m1/m2
rules per the USER's static-is-flawed direction, derived from further per-game digs; (c) accept
the coupled-GT churn and adopt on doctrine. New instruments landed: `library_top` scenario key,
`MTG_FSW_LINE` (tail line dump), `MTG_TRACE_SOLVE_TURN`, `MTG_LIFE_TRACE` (permanent),
`test/scenarios/al_hold_kill_turn.json` (greedy kill-turn guard).

## 2026-08-21c: TWO REAL BUGS UNDER THE GAP (USER: "clairvoyant artifacts can be put aside, but
## if there are other issues we'll need to address them") -- both FIXED; gap re-measured

The decoupled gap was re-attributed per-game: 38 of the (job,gi) cells red under salt 1 are red
under salt 2 WITH THE SAME turn slip (24 distinct games; only 3 games flip direction between
salts). A salt-robust slip cannot be stream luck, so the ~+0.004/game was NOT clairvoyance -- it
decomposed into two mechanisms, one of which was a pair of real bugs:

**BUG 1 -- the `--scenario` harness never stamped deck traits (FIXED).** `RunScenario` built its
GameState without `uses_second_main` / `deck_feeds_combat` / dependency pulls / shuffle salts, so
`MainPhaseFilterActive` (gated on `state.uses_second_main`) was FALSE in every fixture: every
"PHASE arm" scenario probe this arc ran was silently unfiltered, and the 2026-08-21b "machinery
proven complete via pinned scenarios" claim was VACUOUS for the filtered arm. Fix: the stamp block
was extracted to `GoldFishRunner::StampDeckTraits` and is now called by SetupGame, RunScenario and
RunCastOrderReport. Runner byte-identity verified (digest-identical repro game + full smoke).

**BUG 2 -- `ApplyEnablerWipeRecheck`'s `order.size() < 4` fast-out blocked the 3-cast backed
re-arm (FIXED).** The USER-adopted Remedy/Silence alternation (MTG_ORDER_RECHECK, 2026-08-18)
never ran for the subset {Silence, Silence, Remedy} with a Remedy ALREADY live -- exactly the
POST-COMBAT kill shape the enforced split forces (Invigorate is m1, so the m2 interleave is 3
casts). Canonical enabler-first order cast Remedy2 first; the first Silence's wipe killed BOTH
Remedies; the second Silence GIFTED 6 -- so the m2-route T3 kill was inexpressible and the split
arm's hold-lines were undervalued at any budget (g6006_285, g7007_780: PHASE 4->3 after the fix,
now matching CTL). The fix lowers the fast-out to 2 and exits early on wipe-free sets (Reverent
Silence is the only `destroy_all_enchantments` card, so every other deck pays one def-pointer
pass). This also improves the CTL arm's END STATES on GT games (the size-2 backed case [Silence,
Remedy2] now keeps the fresh Remedy out of the wipe): smoke antilife scores IDENTICAL
(4.9010/4.1840/4.1733), 7 play-digests/tier changed with the fix's exact signature
(Remedy;Silence -> Silence;Remedy). Guard: `test/scenarios/al_interleave_kill.json` (depth 1,
passes BOTH arms; d0 cannot assemble the interleave -- a pre-existing greedy limitation in both
arms, noted, not chased).

**Post-fix decoupled gap:** salt1 +23/8000 (+0.0029), salt2 +31/8000 (+0.0039) -- from +29/+36.
CTL under decouple moved 0-1 games. The remaining class has ONE named mechanism:

**The residual (g602-class): dump-plan tie-break, not clairvoyance (salt-robust).** At the T2
root every plan TIES at the projection horizon (e.g. tail=5 for all), the first-verified-win
shortcut commits the FIRST in-horizon winner, and MoveOrderPlans ranks by immediate value -- so
the arm that ENUMERATES a dump plan casts it. Under the split, the collapse_in_hand speculative
emission (built for this arc) offers Invigorate-as-burn with Remedy still in hand, creating
`Remedy+Invigorate` dump plans whose payment taps Ignoble Hierarch -- forfeiting the attack and
wasting the pump, costs that are real but invisible at a tied horizon. CTL never enumerates that
plan (its Invigorate is auto-fire-only, and the TUNED auto-fire hold -- no ready attacker -> no
fire, SpellEffects.h CanAutoFireAltPayload -- refuses exactly this line). I.e. the split's wide
emission bypasses the adopted auto-fire doctrine, and ties then break toward the dump.
Repair options (USER decision -- cast-order/emission tie-breaks are user-reviewed):
(a) narrow the speculative emission of TARGETED pump payloads (target_own_creature) to mirror
the auto-fire hold at emission time; (b) a reviewed tie-break among horizon-tied plans (prefer
attack-preserving / fewer cards spent) -- touches the first-verified-win shortcut, perf-relevant;
(c) accept the residual (+0.003/game decoupled) and adopt/decline the split on doctrine.

## 2026-08-21d: PUMP-WASTE TIE-BREAK ADOPTED DEFAULT ON (USER: "we already have a solution for
## this, we should ensure it is always on") -- prune form measured-rejected, tie-break form green

The USER directed that the auto-fire hold's judgement always apply. Both wirings were built and
measured; the measurement chose the form:

* **PRUNE form (subset-validity delete) -- MEASURED REJECTION.** Deleting must-tap-attacker pump
  subsets cost a clear decoupled regression on BOTH arms, salt-consistent (CTL +47/+52,
  PHASE +80/+82 per 8000; split gap widened to ~+58): the drip alone (3 free damage) is often
  genuinely worth the card -- e.g. gi139's winning line pumps Skyshroud CUTTER, not the tapped
  dork -- and only the search can price card-vs-damage. The heuristic is wrong as a delete.
* **TIE-BREAK form (ordering only) -- ADOPTED DEFAULT ON (`MTG_PUMP_WASTE_GATE=0` reverts).**
  `SubsetPumpWasted` (no ready attacker, or subset cost unpayable without tapping
  FindBestOwnAttacker's pick -- NOT BuildNonCreaturePool, which counts dorks; NOT printed power,
  Hierarch is modelled 0-power+exalted) sets `Plan::pump_waste`; `MoveOrderPlans` sorts flagged
  plans below siblings, beneath `wins_this_turn` (lethal bursts never demote). Because
  first-verified-win commits the FIRST tied horizon-edge winner, ordering IS the tie-break: the
  hold's preference decides exactly where the search has no signal and costs nothing where it
  does. Measured: decoupled CTL **-5/-1** per 8000 (6 faster, 0 slower -- improves the default
  engine), PHASE -7/-4, split gap +23->+21 / +31->+28; coupled suite scores IDENTICAL all
  modes, 0 slower / 0 faster, digest-only (GT rebaselined). g602: PHASE 8->7 (CTL 6; the
  remaining turn is the same game's non-pump residue).

**Residual after 21c+21d (~+21/+28 per 8000 decoupled):** the non-pump dump shapes -- tutor-now
(g5005_275) and Remedy-alone-now (g4004_113) tapping the attacker at a horizon tie. Same
tie-break architecture could carry an attack-forfeit preference for those (a plan whose payment
must tap the best attacker sorts after equals), but that is a NEW judgement, not an existing
adopted heuristic -- propose-and-measure before any default.

## 2026-08-21e: THE FIVECOLOUR CONDEMNATION DOCTRINE ON AL -- measured INERT-AT-COST, not adopted

USER direction (post-pull of the c6adf22b decision-space arc): "within a turn all breakpoints and
phases should use the same condemnation" -- extend 5C's doctrine deck by deck, AL first. Two
levers built (both scoped to the split, both default OFF, off-state byte-identical):
`MTG_AL_CONDEMN` (CondemnsPassedMainPhase -- m2 continues m1's condemnation list) and
`MTG_AL_BP_CONDEMN` (CondemnsConsideredAtBreakpoint -- AL's only mid-phase breakpoint is the
Idyllic Tutor acquisition, per USER).

**Verdict (decouple ensemble, salts 1-2, 8000 games/salt, rebased engine):**
| arm | quality vs CTL | compute vs CTL |
|---|---|---|
| PHASE | +21 / +28 | +14-17% |
| PHASE+CONDEMN | +21 / +31 (condemn effect: 0w/0b and 2w/0b) | +31-33% |
| PHASE+CONDEMN+BP | +23 / +30 (bp effect: 3 games total) | +32-34% |
| PHASE+CONDEMN+SSM (full doctrine) | +36 / +39 | +36-40% |

Order-condemnation BINDS hard (126k searched-space drops per 300 games, `MTG_ROLLOUT_STATS`,
greedy=0 -- the adopted decision-space shape) yet changed ZERO outcomes on salt 1: everything it
deletes is a line AL's search never preferred. Unlike 5C it is not perf-neutral (+14-16% -- the
per-rollout StampM1Hand pool walks outweigh the m2-shrink savings on AL's small m2 sets). The
breakpoint half is inert both ways (the tutor breakpoint is too rare for its predicted
value-changing-acquisition hazard OR any benefit to register). NOTE: the old "m2 re-offer
recovers prune losses" order-condemnation rejection no longer reproduces on the fixed engine --
the re-offer simply no longer matters either way for AL.

Conclusion: the doctrine is SOUND but has nothing to do on this deck -- AL's m2 re-litigation
was never driving outcomes, so uniform condemnation buys semantics at +15% compute. NOT adopted;
both levers kept as instruments. The doctrine roll-out should continue with decks whose m2/
breakpoint surfaces are load-bearing (the 144d4c2d gap note: 5C's own POST-COMBAT breakpoints).

## 2026-08-21f: THE TRANCHE IS HOT -- rescue-rate audit REVISES the "inert" story; the lossless
## program has its counterexample generator (USER: condemned paths should NEVER run; order+rules
## must MAKE the prune lossless; test and re-evaluate until they do)

Instrumentation landed (`[condemn-tranche] walks/vacuous_skips/rescues`, printed at exit like
enum-memo) plus a PROVABLY-neutral vacuous skip (MTG_TRANCHE_VACSKIP default ON, =0 restores:
when no hand card is on the condemned list, filtered == unfiltered and the tranche walk is empty
by construction -- digest-identical verified, suite byte-identical).

**The numbers (single-job probes, searched d3):**
* AL (PHASE+CONDEMN, 300 games): walks 86,560, vacuous 19,086 (18%), **rescues 13,449 (15.5% of
  walks)**.
* 5C (production config, 150 games): walks 185,873, vacuous 34,051, **rescues 23,288 (12.5%)**.

REVISION of 21e's reading: "condemnation changed zero AL outcomes" was NOT "nothing to condemn"
-- the membership rule (declined-at-m1) is wrong at ~1-in-7 armed no-win nodes on BOTH decks, and
the tranche corrects every one inside the projections, which is exactly where the compute went.
The doctrine as shipped is lossless-by-backstop, not lossless-by-rule. The USER's design target
is lossless-BY-RULE (condemned paths never run, not even counterfactually); the rescue counter is
the counterexample generator for that program: characterize the rescue classes (candidate
regimes: budget-truncated declines; interior-projection declines made under shallower
information than the re-ask; value-changed-by-acquisition, the Dragonstorm shape; affordability
edges), refine order/membership per class with USER review + measurement, and the tranche
demotes to an audit assertion when rescues hit zero on held-out + overnight.

NEXT (deferred): a rescue TRACE (condemned card + node context per rescue, sampled) to classify
the 13k/23k counterexamples -- the classes, not the count, decide which rule changes to propose.

## 2026-08-21g: RESCUE TRACE CLASSIFICATION -- the rescues are SIBLING-REDUNDANT, the rule is
## already answer-lossless, and the tranche is (slightly harmful) insurance. PROPOSAL -> USER:
## flip MTG_CONDEMN_TRANCHE default OFF (= the user's "condemned paths never run" design)

**Instrument** (`MTG_TRANCHE_TRACE`, default off, this commit): one stderr line per rescue --
`[tranche-rescue] seed T d cut imm filt resc nfilt ntr w0trunc bud cond acq plan` -- game seed for
repro, depth, filtered-vs-rescued win turns, filtered/tranche plan-set sizes, wave-0 beam
truncation, budget used/limit, WHICH condemned card(s) the plan casts, cards in hand missing from
the m1 stamp, and the plan summary. (Caveat: `acq` counts everything unstamped -- lands,
unaffordable and deferred cards too, not just true acquisitions -- read it loosely.)

**The traced population** (same probes as 21f, one pooled batch: AL 300g PHASE+CONDEMN d3/b10
s5005, 5C 150g production d3/b10 s1001): 36,397 rescues, AL 13,109 / 5C 23,288, in 287/300 and
127/150 games. Shape: 93-98% at d=0 (leaf projections), 87-95% gain exactly 1 turn, plan is
usually the condemned card ALONE (a cheap permanent: Hierarch/Birds/Remedy/Drone; Faeburrow/
Cannons/Greaves/Oko), and in AL 96% of rescues fire where the filtered m2 offered NOTHING but
pass. 5C only: 39% fire at >=90% budget used. w0trunc=0 for every rescue on both decks.

**The classification collapses to ONE class: SIBLING-REDUNDANT.** Reading the stamp
(StampM1Hand) closed it: a card is only condemned if it was Main1-classified AND jointly
affordable ON TOP of the branch's chosen m1 casts (plain pre-cast pool), and the INITIAL m1
enumeration is deliberately unfiltered -- so for every rescue "branch q declined affordable X,
m2 wants X", the sibling branch q+X EXISTS in the same m1 pass and reaches an equivalent state.
The rescue merely re-derives that line inside branch q, raising q to a TIE; first-strictly-better
then keeps whichever branch move-order visited first. Effect: digest churn, no answer change.
The 21f candidate regimes dissolve: affordability edges and combat-treasure mana are exempted BY
THE STAMP (that is what its joint-affordability test is); shallow-info/budget declines are
covered by the same-pass sibling; no rescue whose value NO m1 sibling could reach (the
combat-timing nonequivalence class, m2-cast strictly better while m1-affordable) was observed.

**The decisive A/B (MTG_CONDEMN_TRANCHE=0 vs on, same probes), coupled + decoupled salts 1,2 --
1350 games:** FOUR per-game diffs total, none salt-robust (pure tie-flips):
* coupled: AL identical scores (digest-only churn); 5C gi124 OFF WINS T7 vs ON T8 (dug: a d0/d1
  rescue at bud=72/9000 re-valued a branch 7->6, tying the sibling; first-verified-win kept the
  earlier-ordered branch; realized play worse -- NOT budget starvation).
* salt1: 5C identical; AL gi100 OFF worse (6->7).
* salt2: 5C identical; AL gi171 ON LOSES the game (wt=-1 -> 9) where OFF wins T8; gi204 OFF
  better (8->7).
Net: OFF better 3, ON better 1. Perf (sequential, no trace): tranche costs ~12% AL / ~9% 5C wall
on these probes. So the tranche never rescued an outcome, flipped one game INTO a loss, and
burns the compute 21e attributed to "the price of the backstop".

**Revised reading of 21f:** "rescue rate 15.5%/12.5%" counted BRANCH-LOCAL re-derivations, not
membership-rule errors. The ordered-condemnation rule as built (Main1-classified + jointly
affordable + declined, stamped per candidate branch, m1 enumeration unfiltered) is ALREADY
answer-lossless on both pilot decks -- lossless-by-RULE, the user's design target, with the
losslessness argument being sibling coverage: (1) stamp exempts anything not jointly affordable
at m1, (2) the unfiltered m1 pass contains q+X for every condemned X, (3) casting X at m1 is
never worse than at m2 for these decks' condemnable cards. (3) is the deck-dependent leg: a deck
with a card whose value is strictly HIGHER post-combat while already affordable pre-combat would
break it -- the audit instrument (=1 + trace + off/on diff) is the per-deck check when wiring
condemnation into a new provider.

**PROPOSAL (USER decision, not adopted):** flip MTG_CONDEMN_TRANCHE default OFF; keep the
tranche + counters + trace as the opt-in audit instrument. This IS the stated design ("my
ordered design is actually intended never to run any condemned paths"), now with the data and
the argument. Pre-adoption gate if approved: full suite (the flip touches adopted 5C
production), expect digest-level churn only; GT rebaseline where digests move.

## 2026-08-21h: TRANCHE DEFAULT OFF ADOPTED (USER); AL-bundle numbers refreshed under the flip

**Adopted** (41b957e7): MTG_CONDEMN_TRANCHE default OFF, `=1` re-arms as the audit instrument.
GT rebaselined smoke + regression (overnight running); all churn confined to 5C keys. Every
slower game classified before accept: smoke d5 gi0 = churn (recovers at 16x); regression gi104 =
fetch-shuffle variance (draws diverge T3); regression d3_s3003 gi57 (same-draws, persists at
16x) DUG: under decoupled salts 1 and 2 the two arms are BYTE-IDENTICAL (both T7) -- the
tranche-on T6 was coupled-stream tie-flip luck, clairvoyance-class, not a coverage gap. The
per-deck wiring check for future condemning providers is documented at the flag site
(TurnSolver) and in 21g: probe MTG_CONDEMN_TRANCHE=1 + MTG_TRANCHE_TRACE=1 vs default, diff
per-game wins under decoupled salts; only a salt-ROBUST on-better game is a real gap, and it is
a membership/order rule bug to fix, not a tranche to ship.

**AL bundle re-measured on the flipped binary** (PHASE+CONDEMN, tranche off; 8-job decouple
manifest, salts 1/2; CTL re-run byte-identical to yesterday's, confirming the flip inert
off-deck):
* Quality: BUNDLE - CTL = +26 / +12 per 8000 (salt1/salt2) -- same band as PHASE alone
  (+21/+28): the residual is still the non-pump dump tie-break class (21d), and condemnation
  adds nothing measurable either way (vs tranche-on bundle: +5/-19, direction flips by salt =
  noise).
* Compute: the old objection is GONE -- bundle wall vs CTL (sequential, same box): d5 jobs ~12%
  FASTER (the m2-shrink now nets its savings without paying the tranche), d3 ~+4%, net ~-6%.
**Open (USER):** AL adoption still gated ONLY by the ~+0.002/game split residual; options are
adopt-on-doctrine, or first build the attack-forfeit tie-break (same architecture as the adopted
pump-waste tie-break) and re-measure.

## 2026-08-21i: AL RESIDUAL DECOMPOSED (class A dump-ties + class B condemnation deletions);
## attack-forfeit tie-break MEASURED-REJECTED; TWO STAMP RULE FIXES land (re-arm pair + cast!=declined)

**Decomposition** (fresh decoupled ensemble, salts 1/2, bundle = PHASE+CONDEMN tranche-off):
bundle-vs-CTL salt-robust worse = 57 key-games. Splitting by "tranche-on recovers it" (PC vs
PCnt per game, robust): **class B = 28** (condemnation deleted a line no m1 sibling covers; the
tranche had been silently rescuing these -- the AL-scale counterexamples 21g's probes were too
small to see) and **class A = 31** (the split's own dump tie-break residual, gi113/gi275 family,
condemnation-independent).

**Class A attempt -- attack-forfeit tie-break: REJECTED.** Built Plan::atk_forfeit
(SubsetAttackForfeit: paid casts uncoverable without tapping FindBestOwnAttacker's pick;
ordering-only, below pump_waste; MTG_ATK_FORFEIT_GATE default OFF). gi113 dug first: the T2 tie
commits "Remedy now, tap Hierarch, combat passes" over "attack now, Remedy next turn" -- but
with the gate on the game STILL loses (the committed line passes the attack even without the
cast: the forfeit is in the line choice, not just the mana). Ensemble: PHASE+F-CTL = +34/+38 vs
PHASE-alone +21/+28 (repaired 5/3, broke 17/12; CTL+F +5). The "keep the attack at ties"
judgment loses to value-order more often than it wins -- pump_waste works because it flags a
provably-DEAD card; this flags a judgment call. Lever kept default-OFF as the rejection record.

**Class B root-caused via MTG_TRANCHE_TRACE on gi285/gi780:** the load-bearing rescue is the m2
interleave kill [Silence, Remedy, Silence] with **Tainted Remedy condemned**. The losslessness
argument's equivalence leg fails exactly here: Remedy-at-m1 is NOT Remedy-between-the-wipes
(the first Silence wipes any earlier copy; the +6 gift converts only under a live Remedy), so no
m1 sibling covers the line. TWO rule fixes to StampM1Hand:
1. **Re-arm pair exemption**: an enchantment lifegain_to_loss enabler is un-condemnable while
   the hand holds a destroy_all_enchantments wipe (and vice versa) -- order-dependent value is
   precisely what a decline-transfer rule cannot judge. gi285/gi780 recover T3 with the tranche
   OFF, byte-identical digests to the tranche-on rescues.
2. **cast != declined**: the stamp recorded the chosen plan's own cast cards (membership is by
   NUMBER, so casting Remedy1 condemned every other copy at m2). Excluded; errs toward exempt.

**Ensemble after the fixes** (PCfix arms): bundle-CTL **+26 -> +17 (salt1), +12 -> +7 (salt2)**
per 8000; class B repaired 7/28 per salt; vs old bundle improved 9/7 games, 1 worse. The fixed
bundle now BEATS PHASE-alone -- a sound condemnation is net positive for the split arm. Suite
smoke 36/36, **0 changed configs** (fixes inert on 5C production seeds).

**Remaining (the classify -> refine loop continues):** ~21 class-B key-games un-repaired --
trace shape is the T2 "condemned DORK cast alone at m2" rescue (cond=Ignoble Hierarch, filtered
space empty), i.e. the dork-at-m2 development line that m1 siblings SHOULD cover but on AL
evidently do not (why the sibling is missing there is the next dig). Class A's 31 dump-tie games
remain open (the recorded candidate repairs: playout-tail value treatment, per-card m2-table
shrink). AL bundle adoption is still the USER's call at ~+0.0015/game avg.

## 2026-08-21j: SEARCHED DORK ATTACK/HOLD (MTG_DORK_ATK_SEARCH) -- the greedy islands' intersection
## gets a search branch; AL bundle reaches ~PARITY with control. ADOPTION -> USER

**USER design (this session):** the dork question sits at the intersection of the two
allowed-greedy islands (attacks + mana allocation) -- "the greedy solution really cannot
effectively answer whether we need the dorks or not"; "limit it as much as possible with (safe)
heuristics and search the rest"; 0-effective-power dorks (incl. exalted/pumps) never attack and
just stay (most dorks -- lone-attacker exception aside); vigilance -> freely attack; "freely
attack if there is nothing we could need them for in main 2" (vacuity); dedup identical dorks.
The root-caused motivator: HoldManaSourceForCollapsedMain is a SIX-clause greedy tower (gi=839/
215/174/76/530/230/9) that still forfeited gi113's winning chip -- each clause a per-game patch.

**Built (default OFF):** DorkAtkContested (the heuristics close every obvious case; the hold's
own needs_creature_mana trigger IS the vacuity rule; vigilant dorks exempted inside the hold);
FSLineWin wave-0 evaluates BOTH combat variants at contested nodes only; RELEASE WINS TIES by
weak dominance (the hold's payoff expires at end of turn -- the hold rule's own premise -- while
the chip is banked past the horizon; gi113's tails tie at the edge and the forgone chip is
exactly the missing point); Plan::atk_dork_release -> m_atk_release_pin -> DeclareAttackers
(discard-pin pattern) keeps executor combat lockstep with the scored line. Bug found on the way:
the choice was recorded on win-returns but NOT in the best-update block -- the pin never reached
the executor until that line was added. [dork-atk] trace (MTG_DORK_ATK_TRACE) is the branch's
instrument. v2 (NOT built): the HOLD direction for >=1-printed-power dorks (Deathrite/Bloom
Tender class, today unconditionally attacked) -- touches adopted 5C production, own proposal.

**Verified:** gi113 recovers T4 under BOTH salts; flag-off byte-identical (suite smoke ALL PASS,
0 changed); class-B exemplars unaffected (T3).

**Ensemble (bundle = PHASE+CONDEMN+stamp-fixes +DORK_ATK, salts 1/2):** bundle-CTL
**+17 -> +5 (salt1), +7 -> -4 (salt2)** per 8000 -- ~PARITY with control (salt2 now better);
improved 26/26 games per salt vs 13/14 worse; salt-robust: 24 better vs 4 worse (6:1). The
day's arc total: salt-robust bundle-worse-than-CTL games 57 -> 25. Perf: +7.7% over the
stamp-fixed bundle, ~+3.7% net vs CTL.

**ADOPTION (USER):** the full AL doctrine bundle (MTG_AL_PHASE + MTG_AL_CONDEMN + this branch
default-on) now measures ~neutral on quality (+5/-4 per 8000) at ~+4% compute, with the
doctrine goals met: searched interior m2, one condemnation across the turn (lossless by rule +
sibling coverage), searched dork combat, no greedy interior main. Remaining salt-robust
residual: 25 games (dump-tie class A remainder + un-repaired class B). Options: adopt the
bundle default-on for AL (GT rebaseline expected: AL keys churn); keep measuring; or continue
the classify->refine loop on the remaining 25 first.
