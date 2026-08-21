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

### 2026-08-21 GT-audit addendum (correction of c341c60b's attribution)
Post-rebase per-game audit of ALL 144 overnight gt_logs vs the morning accept: 141 identical;
the only 4 changed games are Dragonstorm gi118 (d3+d5, 6->5) and gi148 (6->5) -- the three
"same-draws persistent slowdowns" the morning accept attributed to c6adf22b as adopted-lever
cost -- now REVERSED by upstream's sac-fold commit, plus gi188 (churn-classified) flipping 7->8.
CORRECTION: those three games were a real c6adf22b regression that upstream later fixed, not a
deliberate adopted-lever cost. The accept itself was procedurally sound (proven not ours via the
MTG_CONDEMN_TRANCHE=1 env test + c6adf22b worktree binary; small; self-corrected by the next
upstream commit), but the story in that commit message was over-generous to the drift. Every
fivecolour overnight game on the merged binary is per-game identical to the inspected morning
state -- sac-fold contributed digest churn only there, and the flip's net-zero verdict stands.

## 2026-08-21k: FULL RESIDUAL CENSUS (all 25/16 looked through) + g8 ROOT CAUSE -- pre-compaction record

**Census of the 25 salt-robust bundle-worse key-games (16 distinct):** tranche-recovery and
4x-budget arms run on ALL of them. NONE recover with budget (all systematic). Buckets:
* EARLY LAND-ORDER STREAM TIES (5): g194/g491/g592/g863/g963 -- first divergence is a T1-T3
  land choice/timing tie; fetches reshuffle; the physical game diverges. The recorded
  fetch-clairvoyance class: salt-robustness does NOT clear it (the real stream is seed-fixed
  under both salts); the matching lottery winners sit in the 24-better set. Luck, not capability.
* DUMP / TUTOR-NOW TIES (6): g14/g147/g554/g838/g860/g408 -- bundle casts sooner at a tie
  (Remedy+Invigorate T3 dumps, Idyllic-Tutor-now). Known class-A residual; attack-forfeit lever
  already measured-rejected against it.
* KILL-TURN SELECTION (4): g469/g666/g852/g648. g852 DUG: ctl casts BOTH Fiery Justices at T4
  m1 (20 dmg, kill); bundle (Justice deferred to m2 by doctrine) casts ONE Justice per m2 across
  T4/T5. USER: "we should be able to cast multiples with this design" -- whether this is an m2
  duplicate-cast expressibility gap or the attack tapping a needed source is THE FIRST
  POST-COMPACTION DIG (test: MTG_M2T_TRACE=1 MTG_M2T_TURN=4 on al_d3_s7007 gi852 seed 7859 --
  does [Justice, Justice] appear in the m2 plan set, and what is the post-combat pool?).
* g8 -- ROOT-CAUSED (two stacked mechanisms, NOT a simple condemnation deletion):
  1. FACTS: both arms identical thru T3 combat (opp 6, two spawns, one power-6). Ctl kills with
     m2 Swords to Plowshares (exile spawn, controller-gains-power rider -> 6 damage under live
     Remedy). Bundle passes m2, kills T4. Tranche-on recovers T3; the rescue trace names
     cond=Swords, plan=[Swords] alone, imm=1.
  2. The T3 turn REPLAYED A LINE COMMITTED AT T2 (MTG_FSW_TURN=3 shows ZERO top-level d3 nodes
     -- no re-solve). In the T2 solve's salted projections the kill does not exist (the REAL T3
     draw, free Skyshroud Cutter, plus StP's 6 is what makes T3 lethal; projected tails tie at
     4 with or without Swords -- 903 trace lines, no Cutter+Swords plan, no tail=3), so the
     projected m1 "declined" Swords legitimately UNDER PROJECTED INFO. The stamp then condemned
     it, and the filtered m2 could not re-offer it when the real state made it lethal.
  3. CONTROL survives the same staleness because its GREEDY m2 re-litigates every turn -- the
     safety net the split removed by design. The split's searched m2 sees only what the
     committed line saw.
  RULE-GAP STATEMENT: "declined at m1" is only a sound condemnation basis when the decline was
  made AT THE EXECUTED STATE with the card a live option; a decline inherited from a stale
  committed line (pre-dating the draw that makes the card lethal) must not condemn. The
  newly-drawn exemption covers the drawn card itself but not cards made newly-LETHAL by the
  draw. Candidate fixes (USER review, post-compaction): exempt the whole stamp on line-replayed
  turns (a replayed m1 is not a decision), or trigger a line re-solve when the real draw
  diverges from the committed line's projection at a turn boundary.

**Standing state at compaction:** bundle at parity (+5/-4 per 8000), 25-game residual fully
bucketed (5 luck / 6 dump-tie / 4 kill-turn incl. g852+g8 named digs / g648 reveal oddity
unexamined in depth); adoption HELD by USER pending the kill-turn digs; NO implementation until
after compaction (USER).

## 2026-08-21l: g8 ROOT CAUSE CORRECTED -- coupled repro proves an IN-SEARCH condemnation
## expressibility gap; the 21k "stale line + salt" story was the measurement context, not the cause

USER pressed on 21k ("doesn't that mean g8 was exposing a separate mismatch bug?"), which forced
the coupled (no-salt) repro that 21k never ran. Result: **g8 is a REAL production-domain loss,
not a salt artifact, and the true mechanism is simpler and worse than 21k recorded.**

* COUPLED SINGLE-LEVER ISOLATION (all on /tmp/al8.json = seed 6014 gi8 d3 b10, no salt):
  - control                       wt=3
  - MTG_AL_PHASE alone            wt=3
  - MTG_AL_PHASE + MTG_DORK_ATK  wt=3
  - MTG_AL_PHASE + MTG_AL_CONDEMN wt=4   <- condemnation ALONE loses the turn, coupled.
* MTG_FD_TRACE, control and phase-only arms: the T1 solve COMMITS THE WHOLE WINNING LINE --
  `T3 pre:[Tainted Remedy(,Invigorate)]{land=Bloodstained Mire} | 2nd:[Swords to Plowshares]`,
  win=3 VERIFIED at T1, replayed straight through. No staleness, no missing draw: the coupled
  search sees the Cutter and the m2 kill three turns out. The phase split handles this game
  perfectly ON ITS OWN.
* Condemn arm, same trace: that line is UNREPRESENTABLE. Declining affordable Swords at the
  projected T3 m1 condemns it, so FSLineTail cannot cast it at m2 -- every no-Swords-at-m1 plan
  tails 4. The T2 re-solve commits `T3 pre:[Remedy,Invigorate] 2nd:<pass> / T4 [Swords,Drone]`
  (fd-pred: T3 post-combat opp_life=6, Swords in hand, 6/6 on board -- the kill is RIGHT THERE
  and the filter forbids it).
* WHY SIBLING COVERAGE FAILS (the soundness argument's counterexample): the m1-cast sibling
  `T3 pre:[Swords,Remedy,Invigorate]` projects opp_life=5 pre-combat but COMBAT DEALS 0
  (fd-pred T3 pre=5 -> 2nd=5) and tails 4. Casting Swords PRE-combat is not combat-equivalent
  to POST-combat: the extra cast changes tap state (a 4th mana source -- plausibly the pumped
  Ignoble Hierarch itself -- gets tapped, muting the attack) and/or removes the 6/6 the greedy
  attack logic reads. Micro-cause (tap-order vs board-read) NOT yet pinned; the macro-fact is
  measured: m1-cast != declined-then-m2-cast whenever the cast interacts with combat.
* RELATION TO 21k: the salted run's staleness (T3 replaying a T2 line whose projection lacked
  the Cutter) is real but SECONDARY -- it is why the salted tails tied at 4, i.e. why the loss
  showed up in the ensemble. The coupled repro shows condemnation deletes the winning line even
  with perfect projection. 21k's rule-gap statement (stale-line declines) remains a valid
  observation but is NOT g8's root cause and would NOT fix g8 coupled.
* RULE-GAP (CORRECTED): "affordable-but-declined at m1 => condemned for m2" is unsound for any
  card whose resolution interacts with combat (removal of opponent creatures under Remedy = the
  deck's WIN CONDITION; also anything whose cast changes attacker tap state). Same family as the
  re-arm pair exemption: the decline is a POSITIVE sequencing choice, not a pass. Candidate
  fixes (USER review, post-compaction): exempt removal-class targets-opponent-permanents cards
  from the stamp; or exempt cards whose cast would change the combat simulation's input state;
  or stamp only cards whose m1-cast sibling achieves an equal-or-better tail (direct coverage
  check, cost unknown).
* Repro artifacts: logs/al_residual/g8c_{ctl,bun}/ (coupled game logs),
  logs/al_residual/g8_coupled_fsw*.txt (T3 plan/tail traces, memo on+off).

### 21l addendum: the m1-cast sibling's combat-0 is PINNED -- scarcity tap-order taps the pumped
### attacker; three layers interact

USER asked "why can't we cast it M1?" -- we CAN (the plan is enumerated and resolves; opp even
reaches 5 pre-combat, lower than the winning line's 6). It loses at the PAYMENT:
* Swords+Remedy together = {2}{B}{W} = 4 mana; the board has 3 lands (Overgrown Tomb G/B, Temple
  Garden G/W, Stomping Ground R/G) + Hierarch (B/R/G) + BoP (rainbow). A 4th source MUST tap.
* ManaSourceRank scarcity order (spend least flexible first): duals rank 20 < Hierarch tri rank
  30 < BoP rainbow rank 50 -> taps 3 lands + HIERARCH, sparing BoP. Both dorks are 0-power
  (verified cards.json); Hierarch is the Invigorate target and the ONLY attacker (Cutter is
  summoning-sick). Tapped attacker -> combat 0 -> opp survives at 5 -> win T4.
* A sparing assignment EXISTS and wins T3 at m1 too: W=Temple Garden, B=Tomb, generic=
  Stomping+BoP -> Hierarch attacks 5 -> opp 0 in combat. The tap order just never tries it:
  TapForCostShared follows ManaSourceRank with zero attacker awareness, and tap order is a
  greedy island (single assignment, no search branch; dork-atk search overrides HOLDS only,
  never payment taps).
* LATENT SECOND DEFECT: PoolWithoutBestAttacker (SubsetPumpWasted's test) computes
  pool-without-Hierarch = 4 >= 4 and assumes "the payment scarcity order can spare the
  attacker" -- but the payment DOESN'T actually spare; the flag models a sparing the solver
  never performs, so pump-waste under-flags exactly this case.
* The winning sequencing (control's committed line) routes around all of it: Remedy alone at m1
  = 3 mana = 3 lands, dorks untapped, attack 5, Swords at m2 off BoP. **The m1 "decline" of
  Swords IS the choice that keeps the attacker untapped -- condemnation deletes precisely that
  choice.**
* FIX MENU REVISED (USER review): (a) attacker-aware tap ordering -- when the cast set includes
  a pump on X / X is the best ready attacker with post-pump power, rank X last (make
  ManaSourceRank read what PoolWithoutBestAttacker already computes); this alone rescues g8
  even with condemnation ON (sibling coverage then holds here) and is a measurable
  heuristic-optimization item; (b) the 21l stamp exemptions for combat-interacting casts --
  still correct in principle (m1-cast is not universally >= m2-cast: blocker info, pre/post-
  combat riders), but no longer the only route for g8.

### 21l correction 2: it is the PREPAY RESERVATION LADDER, not ManaSourceRank
### (the addendum's scarcity-order story was reasoned, not measured -- probes refute it)

USER: "We should check power when tapping. For some reason I thought we did that already." The
second half is right: THREE creature-sparing layers already exist (AttackerReserveEnabled = hold
your greatest-power attacker; DorkReserveEnabled = hold EVERY untapped mana creature;
TapSpareCreaturesEnabled = sort mana creatures to the back of the backtracker's candidate list),
plus ManaSourceRank's rank-60 hold for colourless manlands. What none of them read is POWER of a
specific PERMANENT -- and, as it turns out, that is not what g8 needed either.

SINGLE-LAYER PROBE (bundle = PHASE+CONDEMN, coupled, gi8; wt=4 is the loss, wt=3 the fix):
  baseline 4 | MTG_NO_DORK_RESERVE=1 -> 3 | MTG_NO_BATCH_PAY=1 -> 3
  MTG_NO_TAP_SPARE_CREATURES=1 -> 4 | MTG_TAP_LEGACY=1 -> 4 | MTG_NO_ATTACKER_RESERVE=1 -> 4
MTG_TAP_LEGACY=1 leaving the loss in place FALSIFIES the addendum's ManaSourceRank story. The
cause is BatchPrepayMainCasts' reservation ladder:
1. Cast order puts the free alt-cast Invigorate after the prepay, so at PREPAY time both dorks are
   0 power (proven below), and the combined m1 cost is Remedy {2}{B} + Swords {W} = 4 vs 3 lands
   -- exactly one body must tap.
2. reserved_crea = attacker bit OR every-dork bit; with no depletion land the ladder is ONE rung:
   hold everything. 3 lands cannot pay 4 -> the rung fails -> fall to the UNRESTRICTED solve,
   which holds NOTHING and is free to assign W<-Birds and the generic pip<-Hierarch.
3. The joint solve taps the HIERARCH. Invigorate then pumps FindBestOwnAttacker -- the tapped
   Hierarch is no longer a legal attacker, so the pump lands wherever it lands and combat deals 0.
   The identical cost is payable as W<-Temple Garden, B<-Tomb, generic<-Stomping+Birds, leaving
   the Hierarch up for 5 (4 pump + 1 exalted) = the T3 kill.
4. WHY per-cast payment does NOT lose it (MTG_NO_BATCH_PAY=1 -> wt=3): paying Remedy first
   consumes the three lands, and Swords' {W} then has only ONE legal source -- Birds (rainbow);
   the Hierarch makes B/R/G and cannot pay white. The greedy is forced into the right assignment
   by COLOUR. Only the whole-turn joint solve has the freedom to get it wrong.
This is the SAME failure the ladder was built to fix (Mirrorwing s24 T4, dork vs depletion), one
level in: dork vs dork, where no rung existed.

BUILT (both default OFF, for A/B):
* MTG_TAP_ATTACKER_RUNG (TurnSolver.cpp BatchPrepayMainCasts + SpellEffects.h): track the
  AttackerReserve bit SEPARATELY (DorkReserve currently swallows it into the all-dorks mask) and
  add it as a final ladder rung -- hold JUST the greatest-power attacker when holding every body
  is unaffordable. Lossless by the ladder's own leave-out-if-you-can argument (one extra first
  try; the unrestricted solve still runs). **Recovers gi8's T3 win WITH condemnation on.**
  Robustness note: the rung holds FindBestOwnAttacker's pick and the own-creature pump TARGETS
  FindBestOwnAttacker's pick, so held body and pump target agree BY CONSTRUCTION, not by luck.
* MTG_TAP_POWER_ORDER (SpellEffects.cpp backtracker candidate sort): within the mana-creature back
  group, order by EffectivePower ASCENDING so the DFS spends the cheapest body first. The literal
  "check power when tapping". Permutation-only (Rule 0b safe).
  **INERT on gi8** -- and that is the proof for step 1 above: if the pump had already landed
  before payment, the Hierarch would be a 4/4 next to a 0/1 Birds and this sort would have fixed
  it. It did not, so both bodies are 0 power at prepay time. Power-awareness cannot help where the
  bodies are indistinguishable by power; the ATTACKER identity is the load-bearing information.
  (It also cannot reach the scarcity `produce` path at all: ManaSourceRank takes a CardDefinition,
  not a Permanent, so it cannot see counters/temp pump/animation. Widening that signature is the
  prerequisite for power-aware scarcity ordering, deferred.)
* USER's alternative -- "we could pump the birds instead" -- is the third fix point and a real
  one: OwnPumpTargetCandidates is PROVIDER-OWNED, so the pump could prefer the body the payment
  will not need (and Birds even flies). Not built: it requires predicting the payment from the
  pump site, whereas the rung fixes it at the payment site with the information already in hand.
  Worth revisiting if a case appears where the held attacker is the WRONG body to keep.

### 21l measurement: MTG_TAP_ATTACKER_RUNG passes both tiers clean; MTG_TAP_POWER_ORDER REJECTED

Gate 0 (conventions): flags-off smoke byte-identical, 36/36 PASS, 0 configs changed.

Suite A/B (GT = levers-off control). "searched" = d3/d5 keys, the ones that decide; d0 is the
greedy runner's lighter bar. Only the three dork decks move; every other deck is untouched.

| arm | tier | configs changed | searched slower/faster | d0 slower/faster |
|---|---|---|---|---|
| RUNG  | smoke (s1001)      | 8  | **0 / 5** | 2 / 13 |
| RUNG  | regression (s2002,3003) | 15 | **0 / 1** (39 same-score) | 1 / 12 |
| POWER | smoke              | 5  | 0 / 2 | 1 / 10 |
| POWER | regression         | 10 | **1 / 0** (48 same-score) | 3 / 8 |
| BOTH  | smoke              | 8  | 0 / 5 | 3 / 17 |

* **MTG_TAP_ATTACKER_RUNG -> ADOPT CANDIDATE.** Zero searched-depth slowdowns across 96
  config-runs (3 decks x 3 seeds x 3 depths, two tiers), 6 searched-depth improvements, and it
  recovers the gi8 T3 kill WITH condemnation on. Mirrorwing gains at every smoke depth
  (d0 6.0570->6.0480, d3 5.0267->5.0133, d5 5.1200->5.0933) -- expected, since the ladder's
  origin case (s24 T4, dork vs depletion) is this bug's sibling. Anti-Lifegain gains d0+d3.
  Only fivecolour d0 moves the wrong way, by +0.001 on the lighter bar.
* **MTG_TAP_POWER_ORDER -> REJECTED (recorded rejection; do not re-propose without new
  information).** Net NEGATIVE at searched depth on the bigger tier: mirrorwing_regression_d5_s3003
  4.9000 -> 4.9100, with no searched-depth improvement to offset it, plus 3 d0 slowdowns. It adds
  nothing on top of the rung (BOTH = the same 5 smoke improvements as RUNG alone, with a worse d0).
  This is the empirical answer to "check power when tapping": the layer CAN be made power-aware and
  the measurement says power is the wrong signal here -- bodies that compete for a tap are usually
  power-IDENTICAL at payment time (the pump has not resolved yet), so the sort mostly reshuffles
  ties, and where it does bite it is as likely to spend the wrong body as the right one. WHICH BODY
  THE TURN NEEDS (attacker identity) is the load-bearing information, not how big it currently is.
  Lever kept as the rejection record per the conventions skill; delete on user sign-off.

REMAINING GATES before the rung can be adopted default-on: (1) held-out validation on the
overnight seeds; (2) re-measure the AL bundle (PHASE+CONDEMN+DORK) vs control WITH the rung on
both sides -- the rung removes a whole loss class the bundle was being charged for, so the held
~parity verdict (+5/-4 per 8000) may move.

### 21l: rung at AL scale -- gi8 class ELIMINATED, but the bundle verdict does NOT move

Decouple ensemble (logs/ssm_sweep/al_fix/decouple.json, 8000 games x salts 1,2), loss-penalized
(unwon = 9), rung ON BOTH ARMS -> logs/tap_rung/al/.

* RUNG ALONE on the CONTROL arm (vs the pre-rung cond_CTL_s{1,2} runs, same manifest/binary
  modulo default-off flags): salt1 **0 worse / 1 better**, salt2 **0 worse / 2 better**
  (delta -0.0001 / -0.0003). Strictly non-harmful at 16000 games. Narrow but free.
* gi8 (al_d3_s6006 gi=8) is FIXED: bundle now ties control at wt=3 under BOTH salts.
* BUNDLE vs CONTROL, both with the rung: salt1 41 worse / 35 better (+0.0003), salt2 38 worse /
  39 better (-0.0005), SALT-ROBUST **23 worse : 9 better** (pre-rung: 24 : 4). So the rung removed
  a loss class and more than doubled the salt-robust wins, but the bundle stays at PARITY -- the
  AL adoption is NOT unblocked by this. The surviving salt-robust residual is the KILL-TURN class
  the census already named: al_d3_s7007 gi852 (ctl 4 / bun 5), gi469 (4/5), gi666 (5/6), all
  unchanged by tapping. Those digs remain the blocker.

### 21l precision: the rung works by COHERENCE, not by FindBestOwnAttacker being right

USER: "So FindBestOwnAttacker solves the issue?" -- worth stating exactly, because "agree by
construction" above overstates it.

In gi8 FindBestOwnAttacker has NO insight: both dorks are 0 power at prepay time, so its rank
(EffectivePower descending via OwnPumpTargetCandidates, stable) is a pure battlefield-order tie
break -- Hierarch at index 1 over Birds at index 3. It could have picked either.

What fixes the game is that the RESERVATION and the PUMP consult the IDENTICAL code path (the rung
holds FindBestOwnAttacker's pick; the own-creature pump targets FindBestOwnAttacker's pick), so the
body held untapped is exactly the body that will be pumped. Either consistent pick wins here:
CountExalted has no tapped filter at its definition or any call site (and by CR, Exalted triggers
on the attack; the granting permanent need not be untapped), so pumping the BIRDS and tapping the
HIERARCH also swings 0 + 4 (Invigorate) + 1 (exalted) = 5 = lethal. There were multiple winning
assignments and the engine found none, because the two sides disagreed about which body mattered.
The fix is coherence between them, not a better ranking. (The exalted claim is a CODE READ, not a
measured run -- the measured fact is the log below.)

VERIFIED in the executed game (bundle + rung, coupled, gi8 -> logs/tap_rung/g8_bun_rung/):
  T3 MAIN_1: Bloodstained Mire; Tainted Remedy {2}{B}; Invigorate -> Ignoble Hierarch;
             Swords to Plowshares {W} -> 6/6; Skyshroud Cutter (alt)   [opp 5]
             board: Ignoble Hierarch UNTAPPED, Birds of Paradise tapped
  T3 COMBAT: attack -> opp 0, WIN T3
Note what this means for the condemnation argument: the bundle now wins by casting Swords AT M1 --
the very sibling whose combat-0 made sibling coverage fail. The rung does not dodge the
condemnation rule, it RESTORES the rule's premise for this game.

KNOWN LIMITATION (not a defect, worth recording): the two FindBestOwnAttacker calls are separated
in time -- prepay runs before the turn's casts resolve. If the board changes between them (a hasty
creature cast this turn becoming the best attacker), the hold protects a body the pump will not
use. That is WASTED, never harmful: the rung is a first-try hold with the unrestricted solve behind
it, so the payment is unaffected. Where FindBestOwnAttacker's ranking QUALITY does real work is a
board whose bodies differ in power (a 3/3 dork beside a 0/1) -- there holding the bigger body is a
substantive, and sensible, call.

## 2026-08-21n: RESIDUAL RE-CENSUS UNDER THE RUNG -- 19/23 reproduce COUPLED; PHASE (not
## condemnation) is the bigger cause; gi852 ROOT-CAUSED as a classification/combat mana collision

Re-ran the residual with MTG_TAP_ATTACKER_RUNG=1 on both arms (23 salt-robust worse rows = 15
distinct games; artifacts logs/al_resid2/, manifest /tmp/al_resid_rung.json).

* SHAPE: every one of the 23 is EXACTLY +1 turn -- no losses, no multi-turn slides. 8 games lose at
  BOTH d3 and d5, so this is systematic behaviour, not a depth artifact.
* COUPLED (no salt) repro: **19 of 23 reproduce**, 4 go away, 0 flip better. So the bulk is
  production-domain, NOT a decouple-ensemble artifact. (Running the coupled isolation FIRST is now
  the standing method -- it is what corrected the g8 story.)
* SINGLE-LEVER attribution (coupled, rung on): **PHASE 12 rows / 6 distinct games**, CONDEMN 7 rows
  / 5 distinct, DORK **0**. The phase split ITSELF is the bigger cause -- the earlier framing that
  condemnation was the main culprit is wrong.

### gi852 (al_d3_s7007, USER's flagged game) -- FULLY ROOT-CAUSED

Board at T4: Temple Garden(G/W), Stomping Ground(R/G), Overgrown Tomb(B/G), Blood Crypt(B/R),
Ignoble Hierarch(B/R/G), Birds of Paradise(any) = **exactly 6 sources**; hand holds 2x Fiery
Justice ({R}{G}{W}, 5 damage + "opponent gains 5" which Tainted Remedy INVERTS = 10 each).
Opponent at 20. Two Justices = 20 = exactly lethal, and they need all 6 sources with ZERO colour
slack (Temple Garden and Birds are the ONLY two white sources).

* CONTROL commits `T4 pre:spells[Fiery Justice,Fiery Justice]` -> opp 0, **win T4**.
* SPLIT commits `T4 pre:<pass> | 2nd:spells[Fiery Justice]`: Fiery Justice is classified MAIN-2, so
  the split's m1 candidate set does not contain it; m1 passes; the greedy attack swings the
  Hierarch alone for 1 (0 power + exalted); that taps the 6th source; m2 now has 5 of the 6 mana
  and can afford ONE Justice. 10 damage instead of 20 -> **win T5**. The turn TRADED 1 DAMAGE FOR 10.
* MTG_UNPRUNED=1 restores the m1 candidate set (Fiery Justice appears 1694x at T4 m1 vs **0**
  pruned) and the split then commits `pre:spells[Fiery Justice,Fiery Justice]` -> **win T4**.
* USER's hypothesis ("we should be able to cast multiples with this design, so 4 is confused") is
  CORRECT about the design and the block is NOT expressibility: PlanGroupKey returns the HAND INDEX,
  so two copies are two independent groups, and the m2 enumerator demonstrably emits same-card pairs
  (`Ignoble Hierarch,Ignoble Hierarch` appears in the T4 m2 trace). The pair is absent only because
  after the attack it is genuinely UNAFFORDABLE. Neither MTG_MAIN2_DROP=1 (m2 uses the m1
  enumerator) nor MTG_ENUM_MEMO=0 nor MTG_COLOR_EXACT=0 changes the result; only MTG_UNPRUNED does.
* CLASS: same shape as g8 -- an m1 -> m2 deferral that the intervening COMBAT invalidates by
  consuming a mana source the deferred cast needs. g8 was the payment tapping the attacker; this is
  the attack tapping the payment. Both say the same thing: **main-1 vs main-2 placement cannot be
  decided without modelling what combat does to the mana pool.**

### OPEN THREAD (do not guess -- measure): the dork search prefers HOLD and is overridden

MTG_DORK_ATK_SEARCH fires at T4 (122 contested nodes) and prefers hold: `[dork-atk] T4 d1 hold=5
rel=6`. Yet the executed PD game still ATTACKS at T4 (and still wins T5). Two unexplained facts:
(1) the preference is not reaching the executor on a turn REPLAYED from the T3-committed line;
(2) hold projects 5, not 4 -- if holding truly leaves all 6 sources up, the m2 pair is affordable
and the branch should see the T4 kill. Every [dork-atk] node at T4 is d1 (one at d2); the
TOP-LEVEL T4 decision never runs one. Next step is to instrument the hold branch's m2 pool
directly rather than infer it.

### The MTG_UNPRUNED equality across all 23 -- and why it is WEAK evidence

At equal pruning (MTG_UNPRUNED=1 on BOTH arms) bundle == control on all 23 jobs, 0 worse, 0 better.
That is consistent with "the whole residual is prune deletions", but it is close to TAUTOLOGICAL and
must not be quoted as proof: UNPRUNED restores main-2 cards to the m1 set, which is exactly what the
classification prune removes, so it largely DISABLES the levers under test. (It also costs the
control arm real quality -- 14 of 23 jobs get WORSE unpruned, budget dilution from widening -- so
`bun_unpruned` vs `ctl` is not a valid comparison either.) The load-bearing evidence is the
per-game structural dig above, not the aggregate equality.
