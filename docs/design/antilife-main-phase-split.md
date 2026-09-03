# Anti-Lifegain: the enforced main-1/main-2 split (MTG_AL_PHASE) — built, measured, DECISION OPEN

**Status (updated 2026-09-03): ADOPTED.** The full AL doctrine bundle — `MTG_AL_PHASE`,
`MTG_AL_ORDER`, `MTG_AL_SSM`, `MTG_AL_CONDEMN`, `MTG_AL_DORK_M1`, `MTG_PHASE_DAMAGE_BOTH` — is
DEFAULT ON as of a760a9d2 + b5a5e4e8 (2026-08-22); see "ADOPTION (USER)" later in this doc. The
"DECISION OPEN" title and the default-OFF lever table below are the pre-adoption record.

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

## 2026-08-21o: THE DORK SEARCH IS ONE-DIRECTIONAL -- it can only turn a HOLD into an ATTACK,
## never an ATTACK into a HOLD. gi852 needs the direction that does not exist.

USER: "I thought we already implemented attacker searching? In that case it should know to hold
here?" We did, and it does not -- the trigger is gated on the greedy's PRIOR verdict.

TWO CODE GAPS, both release-only:
1. `DorkAtkContested` (DecisionProviders.cpp): `if (prov.AttackWith(s, p)) { ++natural_attackers;
   continue; }` -- a dork the greedy WANTS to attack with is counted as a natural attacker and can
   never be contested (held_n stays 0 -> early `return false`). The branch only ever asks "the mana
   hold is holding this dork; should it swing?" In gi852 the greedy attacks the Hierarch (0 power
   but 1 damage via lone-exalted), so held_n == 0 and the search is SILENT.
2. `AIEngine::DeclareAttackers`: only `pin == 1` does anything -- its own comment says "pin==0/none
   is the natural heuristic". So even if the search CHOSE hold, the executor cannot express it.
   Benign while (1) holds (hold == natural), but it means the mechanism has no hold direction at all.

PROOF (not inference -- an executor-only force was NOT enough, see below):
* New DIAGNOSTIC `MTG_DORK_FORCE_HOLD_TURN=<turn>` (default 0 = INERT, one site in
  HoldManaSourceForCollapsedMain, applied in search rollouts AND the executor): forces every mana
  dork to hold on that turn. Smoke with it unset: **36/36 PASS, 0 configs changed** (byte-identical).
* gi852 with `MTG_DORK_FORCE_HOLD_TURN=4`: the committed line becomes
  `T4 pre:<pass> | 2nd:spells[Fiery Justice,Fiery Justice]` -> opp 20 -> 0, **win T4**. So:
  - the hold is worth the whole turn, and
  - **the m2 enumerator has NO duplicate-cast gap** -- it emits the same-card pair the moment the
    mana is there. 21n's "affordability, not expressibility" reading is CONFIRMED with direct
    positive evidence (the earlier m2t trace showing 0 pairs was a node-coverage artifact).
* METHOD TRAP worth keeping: forcing the hold in the EXECUTOR ONLY (`!m_in_rollout`) left the game
  at T5 and looked like an enumeration gap -- because the m2 plan was REPLAYED from a committed
  line built on the assumption that combat happened. A combat-side intervention must be applied
  inside the SEARCH too, or the committed line silently overrides it.

VALUE OF THE MISSING DIRECTION (coupled, 23 residual jobs, force at T4 / T5):
* recovers **8 of 19** coupled-worse rows = 4 distinct games (gi852, gi469, gi666, gi963, each at
  d3 and d5) -- i.e. 4 of the 6 PHASE-attributed games. gi592 and gi554 are NOT hold cases.
* collateral on this subset: **0 of 23 hurt** by the blanket force. NOTE this is a biased subset
  (already selected as bundle-worse); a blanket force on a full run would certainly have collateral,
  so this bounds the DIRECTION's value, not a shippable policy.

PROPOSAL -> USER (not adopted; this is a WIDENING, which Rule 0c makes a user-review gate):
make `DorkAtkContested` symmetric -- also contest a dork the greedy WANTS to attack with when main
2 has a use for its mana (the vacuity test the hold tower already computes), and give the executor
pin a real `pin == 0` hold branch. The perf worry USER raised ("to avoid this becoming much
slower") is the reason to gate it on the m2-need test rather than on every attacking dork.

## 2026-08-21p: SYMMETRIC DORK CONTEST BUILT (USER-approved) -- MTG_DORK_ATK_HOLD_DIR

USER: "Yes, we should contest the dork when main 2 has a use for the mana." + "vigilance dorks like
Faeburrow Elder are exempt ... they can and should freely attack."

BUILT (default ON inside MTG_DORK_ATK_SEARCH, which is itself still default OFF; MTG_DORK_ATK_HOLD_DIR=0
reverts for A/B):
* `DorkAtkContested` -> `DorkAtkContestedKind`: 0 none / 1 greedy HOLDS -> search the RELEASE
  (pre-existing, checked FIRST so its verdict is unchanged wherever it fires) / 2 greedy ATTACKS
  with a non-vigilance mana dork whose mana the deferred main would spend -> search the HOLD.
  **VIGILANCE EXEMPT** on this side too: a vigilant dork does not tap to attack, so its mana
  survives combat and there is nothing to contest -- Faeburrow Elder swings for free.
* `Main2SpendsCreatureMana` -- the "main 2 has a use for the mana" gate, and the reason this does
  not contest every attacking dork (USER's standing perf bar). It GENERALISES the hold tower's
  `needs_creature_mana`, which asks only whether ONE hand card is affordable with the dorks and not
  without -- the test gi852 slips through, since each Fiery Justice (3) is affordable off 4 lands
  and only the PAIR needs the dork. It accumulates the cheapest eligible casts up to what the board
  can pay and asks whether that spend exceeds the non-creature sources. Deliberately optimistic: a
  TRIGGER for the search, not a decision.
* `M2ManaCandidate` -- the tower's three measured hand-card exclusions (free-alt gi=531, unbacked
  gift-damage gi=839, unbacked gift-ETB gi=215/...) extracted to ONE reader now that two scans use
  them, per the repo's no-twin-copies rule.
* Tie rule: **RELEASE WINS TIES IN BOTH DIRECTIONS**, the same weak-dominance argument applied
  consistently -- the hold's payoff expires at EOT, a swing is banked past the horizon. So the
  alternative takes ties when it IS the release (<=) and must be STRICTLY better when it is the
  hold (<).
* Executor pin gains a real hold branch: plan value 2 = forced hold (override 0); 1 = forced
  release; 0/none = natural. Previously `pin == 0` was a no-op, so "hold" was inexpressible.

GATES:
* `MTG_DORK_ATK_HOLD_DIR=0` reproduces the pre-change dork search EXACTLY on all 23 residual jobs
  -- the tower/trigger refactor is behaviour-preserving.
* Clean-env smoke: 36/36 PASS, 0 configs changed (byte-identical).

MEASURED (AL decouple ensemble, 8000 games x 2 salts, rung on everywhere):
* vs release-only: salt1 6 worse / 5 better, salt2 2 worse / 7 better, **SALT-ROBUST 0 worse : 4
  better**. Strictly non-harmful, small gain.
* vs CONTROL: **salt-robust residual 23 -> 18 worse** (better unchanged at 9); salt1 +0.0005,
  salt2 -0.0011. gi852 and gi963 now match control at both depths.

STRUCTURAL LIMIT worth recording (why the blunt force recovered 8 rows and the real trigger 4):
`MTG_DORK_FORCE_HOLD_TURN` also fixed gi469 and gi666, but NOT via the combat choice for a given
plan -- it changes the projections from T1 on, so a DIFFERENT m1 plan gets committed. gi469's real
fix is not casting Birds of Paradise at T4 m1 (which spends the mana the deferred Fiery Justice
needs); the trace confirms no T4 node ever reaches `alt(hold)=4`, so the kill is unreachable given
the m1 plans explored. The branch decides COMBAT AFTER the m1 plan is fixed and cannot re-choose
it. Making the combat variant part of plan enumeration would reach these, at a multiplied plan
space -- NOT proposed; recorded as the boundary of this lever.

## 2026-08-21q: ADOPTION READINESS -- no unrecoverable games; what is and is not still open

USER: "Are there any remaining unrecoverable games? i.e. Anything blocking adoption?"

### No. Nothing in the residual is unrecoverable.
* All **18** salt-robust worse games are **exactly +1 turn**; **ZERO become unwon**.
* The bundle WINS MORE GAMES than control under BOTH salts: 7972 vs 7969 (salt1), 7980 vs 7975
  (salt2). It converts 4-5 control losses into wins per salt.
* Exactly ONE game where control wins and the bundle does not: al_d3_s5005 gi43 (ctl 8, bun unwon).
  It is NOT salt-robust -- under salt 2 BOTH arms lose it. It is the same +1-turn class landing on
  the max_turns boundary, not a new failure mode.
* Full delta spread per 8000: salt1 {-4:1, -2:1, -1:28, +1:36, +2:1}, salt2 {-2:3, -1:35, +1:32}.

### Gates PASSED
| gate | result |
|---|---|
| clean-env smoke (all levers off) | 36/36 PASS, 0 configs changed (byte-identical) |
| `MTG_DORK_ATK_HOLD_DIR=0` vs pre-change search | identical on 23/23 residual jobs |
| smoke, rung only | searched **0 slower / 5 faster** |
| regression tier, rung only | searched **0 slower / 1 faster** |
| regression tier, FULL STACK (rung+phase+condemn+dork+holddir) | searched **0 slower / 12 faster**, every changed key neutral-or-better across all three dork decks |
| AL 16000 games, rung alone on control | 0 worse / 1-2 better per salt |
| AL 16000 games, hold direction vs release-only | salt-robust **0 worse : 4 better** |

### Still OPEN (the honest list)
1. **Overnight held-out validation has not been run** -- the standard final gate (validate the
   winner on the disjoint overnight seeds). This is the one genuine PROCESS blocker. Launched.
2. **Perf: ~+8% compute on Anti-Lifegain** for the full stack (6 alternating runs on a quiet box,
   1600 games: control mean 6040 ms, full stack 6542 ms). The rung alone is ~+2-3%. Within the
   range previously accepted, but it is not free and it is a USER call.
3. **The AL aggregate is PARITY, not a win**: salt1 +0.0005 (worse), salt2 -0.0011 (better). The
   real gains are elsewhere -- more games won, and the salt-robust residual 23 -> 18. Whether
   parity-plus-fewer-losses justifies +8% compute is a judgment call, and it is the USER's.
4. **Follow-through IF adopted**: GT rebaseline across all 3 tiers, and the Anti-Lifegain
   **value leaf must be regenerated** (artifacts fingerprint play behaviour); consider the keep
   model too. Work, not a blocker to the decision.
5. **Scope**: the RUNG is deck-agnostic (mirrorwing and fivecolour both gain) while AL_PHASE /
   AL_CONDEMN are AL-scoped. They can be adopted SEPARATELY -- on the evidence the rung is the
   cleaner, more clearly-positive candidate and does not need the bundle decision to land.

## 2026-08-21r: OVERNIGHT HELD-OUT VALIDATION -- it OVERTURNS the rung recommendation

Three overnight (held-out, disjoint-seed) runs against GT, game-weighted, loss-penalized. This is
exactly the gate that exists to catch small-sample overfit, and it caught one.

| arm | searched: games / net turns / per-game | d0 per-game | decks touched (searched) |
|---|---|---|---|
| rung only  | 13900 / **+0.01** / +0.00000 | -0.00375 | antilife, fivecolour, hinata, mirrorwing |
| dork only  | 2800 / **-22.98** / -0.00821 | 0 (untouched) | **fivecolour only** |
| full stack | 13900 / **-22.98** / -0.00165 | -0.00375 | all four |

### The rung does NOT validate, and it SUBTRACTS from the dork search
* Per-deck, rung alone (searched): mirrorwing **-5.98** turns, fivecolour **+5.00**, hinata
  **+0.99**, antilife 0.00 -> **net +0.01 turns over 13900 games = exactly neutral.**
* Its smoke (0 slower / 5 faster) and regression (0 slower / 1 faster) cleanliness was SMALL
  SAMPLE. On the held-out seeds the fivecolour effect flips sign and cancels the mirrorwing gain.
* Worse: the full stack's fivecolour gain is **-14.99** while the dork search ALONE delivers
  **-22.98** on the same deck. The rung eats ~8 turns of the dork search's gain (consistent with
  its own +5.00 standalone fivecolour cost). Adding it to the dork search makes fivecolour WORSE.
* It IS better at d0 (-0.00375/game, -105 turns over 28000) -- but d0 is the greedy runner's
  lighter bar, and the searched tier is what ships.

### The dork search + hold direction is the clean winner
* **-22.98 turns on fivecolour and ZERO effect on every other deck** (antilife, mirrorwing, hinata
  all byte-identical under dork-only). A single-deck, single-direction, unambiguous gain on
  held-out seeds. Note the beneficiary is FIVECOLOUR, not the deck it was designed for.
* The full stack's ENTIRE held-out gain (-22.98) equals the dork search's alone.

### AL phase + condemn
* antilife -3.00 turns over 8000 games in the full stack (0 under rung-only, absent under
  dork-only), i.e. **-0.00038 per game**. Real but marginal, against ~+8% compute.

### REVISED RECOMMENDATION (supersedes 21q's "the rung is the cleaner candidate")
1. **ADOPT the dork search + MTG_DORK_ATK_HOLD_DIR.** Held-out clean, single-deck gain, no
   collateral. (Its cost lands on fivecolour's compute, to be quantified before shipping.)
2. **DO NOT adopt MTG_TAP_ATTACKER_RUNG on this evidence.** Net-zero held-out at searched depth
   AND it degrades the dork search's fivecolour gain. Keep it default-off as a measured negative.
   NOTE this does not retract the g8 DIAGNOSIS -- the ladder gap is real and the rung does fix
   gi8 -- it says the fix as built does not pay for itself across decks. A fivecolour-scoped
   exclusion, or a narrower rung, is the open direction.
3. **AL phase+condemn is a USER judgment call**: -0.00038/game on antilife for ~+8% compute, with
   an 18-game salt-robust +1-turn residual whose causes are documented (21n/21o/21p).

## 2026-08-21s: CONDEMNATION'S DAMAGE ROOT-CAUSED AND FIXED -- two rule gaps, 24/24 games

USER: "Condemnation should not make it worse. We need to root source that and every other case."
Correct -- condemnation is lossless BY RULE, so a cost is a rule violation, not a tuning result.

METHOD: isolate condemn-on-top-of-phase per game (coupled, 8000 games, the exact overnight AL
config), then partition with the tranche instrument (which re-offers condemned plans).
* condemn vs phase: **24 worse / 13 better / net +8 turns** -- matching the overnight's +8 exactly.
* **ALL 24 are recovered by re-offering the condemned plan.** Every one is a plan condemnation
  deleted that was genuinely better. (This refutes the 21g "tranche rescues are sibling-redundant"
  verdict that the tranche-off flip rested on -- for THIS configuration.)

### ROOT CAUSE 1 -- A PASS IS NOT A DECLINE (22 of 24)
When the chosen m1 plan casts NOTHING, the stamp condemns EVERY affordable card in hand, so the
line "pass at m1, see combat, cast at m2" -- the entire reason a second main exists -- becomes
unrepresentable, leaving the search only {cast at m1, cast not at all}.
* AL gi147 T3: phase-only plays a land, PASSES, swings the lone Hierarch for 1, then casts Tainted
  Remedy at m2 and KEEPS Invigorate for T4's lethal -> win T4. With condemnation the pass condemns
  Remedy AND Invigorate, forcing both out at m1 for a 5-point swing -> win T5.
* A pass is a DEFERRAL: nothing was chosen over the card. Fix: `MTG_CONDEMN_PASS_EXEMPT` -- stamp
  nothing when the m1 plan has no CastFromHand. Recovers **22/24**; condemnation's cost +8 -> +1.

### ROOT CAUSE 2 -- NO ENABLER LIVE (the remaining 2 rows = gi648 at d3 and d5)
A GIFT PAYLOAD (hands the opponent life: alt-cost, on-resolve, or ETB) is worth negative damage
with no lifegain-to-loss enabler live and positive damage the moment one is. Judged at the m1 state
with no enabler out the search declines it CORRECTLY -- but that is a statement about the BOARD,
not the card, and the enabler can arrive later in the SAME turn.
* AL gi648 T6: the m1 plan is [Idyllic Tutor], whose BREAKPOINT fetches and casts Tainted Remedy
  and then Skyshroud Cutter, leaving the opponent on 6; phase-only casts Invigorate + Invigorate at
  m2 for exactly 6 -> win T6. Both Invigorates were stamped at the pre-Remedy m1 state where each
  would GIFT 3 -> kill unreachable -> win T7.
* NOTE why a plan-keyed fix cannot work here (my first attempt, measured and discarded): Tainted
  Remedy is not in the plan's actions, nor even IN HAND, at stamp time -- the Tutor puts it there.
  The sound key is the BOARD: `MTG_CONDEMN_ENABLER_EXEMPT` exempts gift payloads whenever no
  enabler is live at the m1 decision.

### RESULT (AL coupled, 8000 games, condemnation's cost vs PHASE-ONLY)
| arm | worse | better | net |
|---|---|---|---|
| plain condemn | 24 | 13 | **+8 turns** |
| + pass-exempt | 2 | 1 | +1 turn |
| + pass-exempt + enabler-exempt | **0** | 1 | **-1 turn** |

**Zero condemnation-caused losses remain across 8000 games.** Held-out overnight confirms:
phase-only +15.00, phase+condemn +23.00, phase+condemn+EXEMPTIONS **+14.00** -- i.e. condemnation
is now marginally POSITIVE rather than -8. Clean-env smoke byte-identical (36/36, 0 configs).

### WHAT REMAINS
The PHASE SPLIT ITSELF is still +14 turns worse than baseline on held-out AL. Condemnation is
exonerated; the split is now the whole of AL's cost and is the next thing to root-source.

### 21s addendum: the exemptions are NEUTRAL on FiveColour (which already ships condemnation ON)
Held-out fivecolour with both exemptions: 3 keys better, 3 worse, 2 unchanged; searched net
**-0.02 turns over 2800 games = -0.00001 per game**. So the fix pays for itself on the deck that
needed it and costs nothing on the deck already using the rule. (The enabler exemption is inert
there -- 5C holds no lifegain-to-loss gift payloads -- so this measures the pass exemption.)

## 2026-08-21t: THE PHASE SPLIT ROOT-CAUSED -- deferral is the defect, land churn is variance

Same method as 21s: isolate PHASE-vs-BASELINE per game (coupled, 8000 games, the exact overnight AL
config) -> **32 worse / 15 better / net +15 turns**, matching the overnight's +15 exactly. Every
worse game is +1 turn. Then classify the FIRST diverging main-1 decision in BOTH directions:

| first divergence | split LOSES | split WINS |
|---|---|---|
| land / fetch choice | 16 | 13 |
| **cast deferred to m2** | **10** | **0** |
| split casts MORE at m1 | 6 | 2 |

* **The land class is SYMMETRIC (16 v 13) -- it is not misplay, it is variance.** The split's
  restricted m1 candidate set changes the folded land choice; when that land is a FETCH the library
  reshuffles and every subsequent draw differs (gi113 T2: base plays Godless Shrine and holds,
  split plays Bloodstained Mire + Tainted Remedy; the streams diverge and never re-converge).
  Roughly +3 turns net, scattering both ways -- the noise floor of this comparison.
* **The deferral class is 10-0 -- ENTIRELY ONE-SIDED. That is the defect.** Deferred cards:
  Fiery Justice (6 of 10, alone or with Reverent Silence), Invigorate (2), Hierarch+Remedy (2).

### ROOT CAUSE: the classifier prices combat BENEFIT but never combat COST
`ClassifyMainPhase`'s DirectDamage arm returns `MP::Main2` unconditionally -- "pure damage feeds no
attack vs the passive opponent". True, and irrelevant to the real cost: deferring past combat can
make the spell UNAFFORDABLE, because the attack TAPS sources the deferred cast needed. AL gi852 T4
is the clean instance -- exactly 6 mana, two Fiery Justice (10 damage each under Tainted Remedy =
exactly lethal from 20), the lone Hierarch swings for 1, taps the 6th source, m2 affords ONE.
**The turn trades 1 damage for 10.** This is the same family as g8 (there the payment tapped the
attacker; here the attack taps the payment): main-1-vs-main-2 placement cannot be decided without
modelling what combat does to the mana pool.

### FIX: `MTG_PHASE_DAMAGE_BOTH` (default off) -- classify DirectDamage as BOTH, not Main2
BOTH, not Main1: this REMOVES a prune instead of installing the opposite one, so the search decides
the phase per state (the search-primary bar), and the documented "pre-combat is actively worse for
Hinata (Crackle destroys its own discount targets)" case stays reachable -- the search just picks
m2 there.

| AL coupled, 8000 games, vs BASELINE | worse | better | net |
|---|---|---|---|
| phase split (BEFORE) | 32 | 15 | **+15 turns** |
| phase split + DAMAGE_BOTH (AFTER) | 17 | 12 | **+5 turns** |
| DAMAGE_BOTH vs plain split | 3 | 15 | **-10 turns** |

The -10 recovered is exactly the deferral class's size. The residual +5 (17 v 12) sits at the
land-variance floor measured above (16 v 13).

GATES: clean-env smoke byte-identical (36/36, 0 configs changed). Regression tier with the whole
new stack (phase + condemn + both exemptions + DAMAGE_BOTH + dork search): searched **2 slower /
9 faster**.

### 21t addendum: AL HELD-OUT PROGRESSION -- the arc now nets a real gain for Anti-Lifegain
Searched tier, 8000 held-out games, vs ground truth (negative = better):

| arm | keys worse/better | net turns | per game |
|---|---|---|---|
| phase only | 6 / 1 | +15.00 | +0.00187 |
| phase + condemn | 7 / 1 | +23.00 | +0.00288 |
| phase + condemn + dork | 3 / 4 | -1.00 | -0.00013 |
| phase + condemn + EXEMPTIONS | 6 / 1 | +14.00 | +0.00175 |
| **full new stack** (phase + condemn + 2 exemptions + DAMAGE_BOTH + dork) | **2 / 6** | **-5.00** | **-0.00063** |

This retires 21q/21r's finding that "AL gets no benefit": it did not, because two rule defects were
eating the gain. With both root-caused and fixed, Anti-Lifegain is now measurably BETTER than
baseline on held-out seeds (6 keys better, 2 worse), not merely at parity.

## 2026-08-22u: THE RESIDUAL IS RECOVERABLE -- every remaining worse game closes with more
## budget or depth; the stack is ADOPTED (and AL_CONDEMN's "inert-at-cost" verdict is RETRACTED)

USER: "Are the remaining cases unrecoverable? If they are I would like to fix them. Otherwise,
let's rebaseline and push when we are done. (unrecoverable with unlimited budget or depth that is)"

The right bar, and a sharper one than 21q's "nothing becomes unwon". These levers are PRUNES, so a
prune that deletes the winning line is unreachable at ANY budget -- that is what "unrecoverable"
means here, and it would be a defect to fix. A gap that closes when the search gets more work is
budget churn, not a deleted line.

### ANSWER: NO. All eight recover.

Residual defined PER GAME (not per key): full stack vs baseline, coupled, the 8000-game held-out AL
ensemble -- **6 worse / 11 better / net -5 turns**, matching 21t's key-level -5.00 exactly. Every
worse game is +1 turn, none becomes unwon, and one BASELINE LOSS is converted (al_d5_s5005 gi395,
unwon -> T8). Plus the regression tier's 2 searched-slower rows. Both arms escalated at EQUAL config:

| game | production gap | closes at |
|---|---|---|
| al_d3_s4004 gi27  | d3/b10 4->5 | d3/**b20** (2x budget), both 4 |
| al_d3_s5005 gi52  | d3/b10 5->6 | d3/**b400** (40x), both 5 |
| al_d5_s7007 gi179 | d5/b20 5->6 | d5/**b100**, both 5 |
| al_d5_s7007 gi216 | d5/b20 4->5 | d5/**b100**, both 4 |
| al_d5_s7007 gi731 | d5/b20 4->5 | d5/**b100**, both 4 |
| al_d5_s7007 gi904 | d5/b20 4->5 | d5/**b100**, both 4 |
| antilife_regression_d5_s2002 gi142 | d5/b20 5->7 | d5/**b50** (2.5x), both 5 |
| fivecolour_regression_d5_s2002 gi28 | d6/b20 5->6 | d6/**b50** (2.5x), both 5 |

At d6/b400 and d8/b2000 **every pair is identical**. Seven of the eight close at 2-5x budget at the
PRODUCTION DEPTH, so this is budget, not horizon. The split does not delete a line the search cannot
get back; it re-orders which lines the budget reaches first. Nothing here is a defect to fix.

### METHOD TRAP: reproduce a harness row at its RESOLVED depth, not at the case label
The case named `fivecolour_regression_d5_*` actually runs at **depth 6**. The harness manifest
(`test/logs/<mode>/manifest.json`) omits `depth` entirely and pins only `budget_ms`, so BatchRunner
resolves the depth from the deck's `value_play` block -- and FiveColour is the one deck that asks for
6 (`[play] fivecolour_regression_d5_s2002 depth=6 budget=20ms source=value_play(depth)+cli(budget)`).
The first repro forced `--depth 5 --ignore-play-profile` and got 5/5 on BOTH arms, which reads as
"the reported row does not reproduce" when it is simply a DIFFERENT GAME (different depth, and the
profile's play block switched off). Always read the run's `[play]` line, or the harness manifest,
before building a single-game repro. Same family as the explain_game.py trap (it diffs the CURRENT
binary, so flip defaults before auditing).

### AL_CONDEMN: the "MEASURED INERT-AT-COST -- NOT adopted" verdict is RETRACTED
Adoption forced the question of whether condemnation earns its place, since the code comment said it
was inert at +14-16% compute. Measured directly, by dropping it from the adopted stack:

| arm (8000 held-out AL games, coupled, vs baseline) | worse | better | net |
|---|---|---|---|
| phase + DAMAGE_BOTH + dork (**no condemnation**) | 10 | 11 | **0** |
| full stack (+ condemn + both exemptions) | **6** | 11 | **-5** |

Condemnation is worth -5 turns and 4 fewer worse games -- one of them a **win turning unwon**
(al_d5_s7007 gi10, T7 -> loss, which only the condemned configuration avoids). The old "inert"
reading was an artifact of measuring a BROKEN condemnation: taken before the pass-is-not-a-decline
and no-enabler-live gaps were found, it averaged a rule that was deleting real lines and gaining
real ones into a null. **A lever measured while it is buggy has not been measured.**

### ADOPTED (USER, 2026-08-22) -- defaults flipped ON, each keeping its `=0` off switch
`MTG_AL_PHASE`, `MTG_AL_CONDEMN`, `MTG_CONDEMN_PASS_EXEMPT`, `MTG_CONDEMN_ENABLER_EXEMPT`,
`MTG_PHASE_DAMAGE_BOTH`, `MTG_DORK_ATK_SEARCH` (with `MTG_DORK_ATK_HOLD_DIR` already default-on
inside it). NOT adopted, unchanged: `MTG_TAP_ATTACKER_RUNG` (21r, held-out net-zero and it eats the
dork search's FiveColour gain), `MTG_AL_SSM`, `MTG_AL_BP_CONDEMN`, `MTG_AL_PHASE_ROOT`.

Reversibility gates: with the defaults flipped, all six forced `=0` **byte-reproduces the baseline**
run over 8000 games, and the defaults-on binary **byte-reproduces the env-forced full stack**.

### PERF -- the real cost, and 21q's "~+8%" is SUPERSEDED
Single-thread-equivalent job ms on a quiet box (median of 3 alternating runs). 21q's +8% was an
AL-only measurement on a much lighter config; at the suite's own budgets the cost is bigger:

| cell | OFF | ON | delta |
|---|---|---|---|
| al_d3 (300g, d3 b10) | 17,956 | 20,408 | +13.7% |
| al_d5 (250g, d5 b20) | 8,217 | 12,967 | +57.8% |
| fc_d3 (200g, d3 b10) | 193,601 | 197,215 | +1.9% |
| fc_d5 (100g, d6 b20) | 112,019 | 162,613 | +45.2% |
| **total** | 331,793 | 394,025 | **+18.8%** |

Per-lever decomposition (single lever vs all-off, same jobs):

| lever | al_d5 | fc_d5 |
|---|---|---|
| DORK_ATK_SEARCH | +2% (digest UNCHANGED) | +19% |
| PHASE_DAMAGE_BOTH | +17% | +2% (digest UNCHANGED) |
| CONDEMN exemptions | inert (digest unchanged) | +22% |
| AL_PHASE+CONDEMN+exemptions | +30% | inert |
| all six | +66% | +46% |

NOISE FLOOR: the exemptions are provably inert on AL (identical digest) yet measured +7%, so read
anything under ~+/-7% as noise. Two things worth stating plainly:
* **DAMAGE_BOTH is nearly free on FiveColour (+2%, digest unchanged at d6)** -- the generic lever
  whose cross-deck cost was the open worry turns out to cost the deck that ships the split nothing.
* **The exemptions cost FiveColour ~22% for a metric-neutral change.** That is the price of the
  search-primary bar, not a regression: an exemption WIDENS the candidate set (it un-condemns), so
  it buys rule correctness with compute rather than with turns.

### 21u addendum: the SMOKE tier's three slower rows also recover -- 11 of 11 across all tiers
Smoke (train seeds) with the adopted defaults: 4 configs changed, searched **3 slower / 2 faster**;
scenarios 14/14; reference reproducibility **0 play-drift / 0 shuffle-dead / 0 enum-gap / 0
mull-drift / 0 contract-fail** over 208 refs. Regression: 8 configs changed, searched **2 slower /
9 faster**, d0 untouched. Escalating the three smoke rows (both arms, equal config):

| game | production gap | closes at | class |
|---|---|---|---|
| fivecolour_smoke_d3_s1001 gi124 | d3/b10 7->8 | d3/**b20** (2x) | budget churn (draws IDENTICAL -- a real line change the search fixes with 2x work) |
| antilife_smoke_d5_s1001 gi70 | d5/b20 4->5 | d5/**b50** (2.5x) | budget churn |
| antilife_smoke_d3_s1001 gi139 | d3/b10 5->6 | **d6/b400** -- NOT at d3/b400 (40x) | fetch variance: DEPTH-limited, not budget-limited |

gi139 is the one row that budget alone cannot fix, and it is the land/fetch class from 21t: the
split's restricted m1 set changes the folded land, the fetch reshuffles, and the two arms play
PHYSICALLY DIFFERENT games from T3 -- no amount of budget at d3 re-converges them, but a horizon
deep enough to value the land choice does. Still recoverable under the USER's bar (unlimited budget
**or depth**), and still not a deleted line.

**Tally across all three tiers: 11 residual worse games, 11 recover.** Nine at 2-5x budget at the
production depth, one at 40x budget (al_d3_s5005 gi52), one only with more depth (gi139).

## 2026-08-22v: OVERNIGHT HELD-OUT GATE -- the stack validates cross-deck, and the one row that
## does not close is a MULLIGAN divergence, not a search failure

Full overnight (all decks, disjoint held-out seeds) with the adopted defaults. Aggregated PER GAME
against the committed gt_logs, loss-penalized:

| deck | tier | games | net turns | worse | better |
|---|---|---|---|---|---|
| antilife | d3 | 4000 | **-5** | 2 | 7 |
| antilife | d5 | 4000 | 0 | 4 | 4 |
| fivecolour | d3 | 1600 | **-17** | 4 | 21 |
| fivecolour | d5 | 1200 | **-5** | 9 | 13 |
| **searched total** | | **73600** | **-27** | **19** | **45** |
| d0 (greedy) | | 96000 | 0 | 0 | 0 |

**ZERO new losses** (no win becomes unwon anywhere). 16 of 144 configs changed.

### The "generic" levers turn out to be scoped by construction
Every other deck -- hinata, burn, auras, goblins, knights, slivers, treasure hunt, dragonstorm,
creature_giving -- is **byte-identical**. MTG_PHASE_DAMAGE_BOTH lives in `ClassifyMainPhase`, which
only runs for a provider that opts into `ClassifiesMainPhases()`, and after this adoption that is
exactly two decks (AL and FiveColour). The cross-deck risk that made DAMAGE_BOTH the last open
worry does not exist: the classifier is unreachable for a deck that does not ship the split. d0 is
untouched for the same structural reason -- every adopted lever is inside the search.

### Residual: 24 rows examined across all three tiers, 22 close
Escalating both arms at equal config (budget 1x/2x/5x/20x, then depth 5/6/8):
* **15 of the overnight's 19 close on BUDGET ALONE** -- 13 at 2x, one at 5x, one at 20x.
* `fivecolour_overnight_d3_s5005 gi301` (fetch reshuffle) closes on **depth** (d5/b100), not budget.
* `fivecolour_overnight_d5_s6006 gi196` (like-for-like line change) closes at **d8/b2000**.
* **`fivecolour gi112` (seed 5005, appearing at both d3 and d5) NEVER closes** -- 5/6 at d5, d6 and
  d8, at any budget.

### Why gi112 is not a counterexample to "recoverable" -- the LOOKAHEAD BOTTOMER, isolated
Its explain says **KEPT HANDS DIFFER**, so the two arms play DIFFERENT PHYSICAL GAMES from turn one
and there is no common line for a deeper search to find. Isolated to the exact mechanism rather than
guessed at (USER: "How does the mulligan diverge?"):

* Both arms take the SAME number of mulligans -- 3 attempts, keep 5. The difference is purely WHICH
  TWO CARDS GO TO THE BOTTOM: baseline bottoms Windswept Heath and keeps Godless Shrine, the adopted
  stack does the reverse. One card.
* `AIEngine::BottomCards` decides that with a **full clairvoyant game ROLLOUT per candidate card**
  (`RolloutWinTurn` over the real post-bottom library), keeping the removal that preserves the
  earliest win and restricting the heuristic tiebreak to the win-optimal set. Those rollouts run the
  ENGINE'S PLAY LOGIC, so any change to play changes their projected win turns and therefore the
  pick.
* PROOF by isolation: with `MTG_BOTTOM_ROLLOUTS=0` (rollouts off, heuristic bottoming alone) the two
  arms keep the **IDENTICAL** hand. With the rollouts on (the default) they differ. The divergence
  enters entirely through the bottomer's rollouts.

**This is NOT model staleness, and the earlier "the keep model was fit under the old play behaviour"
reading of this game was wrong.** FiveColour ships no keep-model sidecar at all; the bottoming
choice is computed LIVE at game start. The bottomer is re-evaluating correctly under the new play
rules and landing on a different card -- a downstream consequence of adoption, not a stale artifact.
(Anti-Lifegain DOES carry an exhaustive keep model, and regenerating the per-deck artifacts against
the new play behaviour remains the standing follow-through from 21q #4 -- but gi112 is not evidence
for it.)

## 2026-08-22w: WHY THE SEARCHED SECOND MAIN LOSES TO GREEDY -- a HORIZON-BLIND LEAF makes a
## self-harming action FREE. Root-caused; fix is a USER call.

USER: "So anti-lifegain is now fully free of greedy solves?" -- NO. `MTG_AL_SSM` is default OFF, so
`SearchedSecondMainInSearch()` is false for AL and the search's INTERIOR second mains still run the
greedy `Solve` at every full-search ply. That hook's own doc calls itself the per-deck adoption route
for the standing directive "search should be truly search at every level."

I flagged that SSM's rejection had the same bad provenance as the retracted AL_CONDEMN verdict
(measured before DAMAGE_BOTH + the exemptions). Re-measured on the adopted stack it is WORSE than
recorded, not better: TRAIN seeds (1001/2002/3003, d3+d5, 6000 games) **29 worse / 4 better / net +31
turns, with 7 rows turning a WIN into a LOSS**. The recorded "+0.0133 churn, all recover at 4-16x
budget" no longer describes it.

### CORRECTION (same session): it IS recoverable -- with DEPTH. Budget alone was the wrong lever.
USER: "With unlimited budget and depth there is no reason why we should miss this. If we are there is
a bug." Correct, and the first escalation below only varied BUDGET. Sweeping DEPTH at budget 10000:

| game | d3 | d5 | d6 | d8 | d10 | greedy (any depth) |
|---|---|---|---|---|---|---|
| gi940 | LOSS | win T8 | win T8 | win T8 | win T8 | win T8 |
| gi970 | LOSS | win T7 | win T7 | win T7 | win T7 | win T7 |
| gi695 | LOSS | LOSS | win T8 | win T8 | win T8 | win T8 |

**With unlimited budget AND depth the searched m2 does NOT miss it -- so this is NOT a bug.** Every
loss is the ordinary depth limitation of a shallow search. The "3 games unrecoverable" claim earlier
in this section was an artifact of escalating only budget; it is retracted. What survives is the
narrower and more useful statement: **the searched m2 needs MORE DEPTH than the greedy Solve to be
safe, because greedy's candidate ORDERING carries domain knowledge that substitutes for depth.** At
the shipped d3/d5 the search is blind to a beyond-horizon cost and the 10-life gift ties with passing;
greedy is never blind, because EvalCard prices the unbacked Aria.

### It is not budget, and not truncation
Escalated on both arms: at the PRODUCTION DEPTH the 3 games stay lost at 20x budget, then at 100x,
1000x and 10000x (budget_ms 100000 vs the production 10) -- budget genuinely moves nothing, which is
why depth turned out to be the lever that matters (see the CORRECTION above). Both memos are exonerated (`MTG_NO_M2_SEARCH_MEMO=1` and
`MTG_SOLVE_MEMO=0` both reproduce the loss), and `BuildBreakpointKey` does fold the condemnation
stamp, so it is not a key collision. It reproduces with every other adopted lever OFF (phase only),
so it is SSM alone, not an interaction with this arc's adoption.

### The decisive comparison
| interior m2 | gi940 | gi970 | gi695 d3 | gi695 d5 |
|---|---|---|---|---|
| greedy `Solve` | win T8 | win T7 | win T8 | win T8 |
| **`MTG_NO_M2_SOLVE=1` (no m2 at all)** | win T8 | win T7 | win T8 | win T8 |
| searched (`MTG_AL_SSM=1`) | **LOSS** | **LOSS** | **LOSS** | **LOSS** |

**At d3 the searched second main is worse than not acting at all** -- it chooses a strictly
self-harming action. (At d6+ it matches greedy everywhere; see the CORRECTION above.)

### ROOT CAUSE: a beyond-horizon cost is scored as ZERO
gi940 play, greedy vs searched: the searched arm casts **Aria of Flame** at T3 m2 and again at T4 m2.
Its ETB is `etb_opponent_lifegain: 10` -- "each opponent gains 10 life" -- which is 10 DAMAGE with
Tainted Remedy out and a 10-life GIFT without it. Opponent 16 -> 26 -> 34. The greedy arm holds
everything and wins at T8 with Remedy + Skyshroud Cutter + Reverent Silence (the gift payloads are
this deck's WIN CONDITION, but only after the enabler).

The metric is loss-penalized win turn with unwon = max_turns+1. At an interior m2 several turns from
lethal, NO candidate reaches a win inside the horizon, so **every plan ties at 9 and the 10-life gift
is free**. Selection is by projected win turn, so the tie is broken by whatever comes next -- and it
picks the gift.

PROOF -- the horizon, not the card, is the variable:

| max_turns | greedy | searched |
|---|---|---|
| 8  | win T8 | **LOSS** |
| 10 | win T8 | **win T8** |
| 12 | win T8 | win T8 |
| 16 | win T8 | win T8 |

At max_turns=10 the searched m2 stops casting Aria entirely and reproduces the greedy line exactly
(Remedy + Cutter + Silence at T8). Two extra turns of horizon are enough for the gift to cost
something measurable, and the search then declines it on its own.

### Why greedy is immune, and why the existing fix does not reach this
`AntiLifegainProvider::ArchetypeCardValue` already prices exactly this card
(`out = enabler_live ? (1+gift)*DMG : DMG - (gift*DMG)/2`), and its comment records the SAME failure
in 2026-08-15 gi=136 -- same card, same +10 at T3, a base win-7 turned into a loss. But that value
feeds **`EvalCard`, the candidate-ORDERING heuristic**; the hook contract says so. Ordering governs
the greedy `Solve`, which is why greedy never picks the gift. The searched path selects by projected
win turn, where a beyond-horizon cost is invisible, so the penalty never binds. **The 2026-08-15 fix
closed the ordering path and the searched-m2 path re-opens the same hole.**

### FIX OPTIONS (not implemented -- shape is a USER judgment call)
1. **Horizon-honest leaf (general).** When no win is reachable inside the horizon, stop scoring every
   plan as a flat `max_turns+1` and break the tie on damage progress / opponent life. This fixes the
   CLASS ("an action whose cost lands past the horizon is free") rather than the card, and it is the
   "HONEST where you SCORE" half of the standing LAW. Blast radius is every deck and a full GT
   rebaseline.
2. **Provider gate (narrow).** Refuse to enumerate an unbacked gift payload in the searched m2, the
   way the m2 hold tower's `M2ManaCandidate` already excludes "unbacked gift-ETB" (gi=215). Precedented
   and deck-scoped, but a blanket ban costs real value: the same ArchetypeCardValue comment records
   that the FIRST pre-Remedy Aria is routinely a strictly-better cast (the verse engine), which is why
   the existing penalty deters the SPARE copy rather than zeroing the card.
3. **Status quo** -- leave SSM off for AL. Cheapest, but it leaves the last greedy solve in the
   deck's search, against the standing directive.

Option 1 is the principled one: the reason greedy currently BEATS the search here is that greedy is
ordered by a life-aware heuristic while the search's leaf is life-blind, and no amount of search
fixes a blind evaluation. Hinata is red on this lever too and is the obvious deck to check next.

## 2026-08-22x: THE FIX -- a MISSING CASE in the unbacked-gift prune (not a leaf change)

USER: "Can you work to fix it?" The dig in 21w pointed at the leaf; the actual defect turned out to
be narrower and already half-written.

### What was wrong
`SubsetHasUnbackedAltPayload` has always refused to EMIT a subset that hands the opponent life with no
`lifegain_to_loss` enabler live -- but it keys on `alt_cost`. **Aria of Flame's gift is an ETB on a
hard-cast enchantment (`etb_opponent_lifegain: 10`), so it was never covered.** The greedy `Solve` was
protected anyway because `EvalCard` prices the unbacked copy through `ArchetypeCardValue`; the searched
path selects by projected WIN TURN, where a beyond-horizon 10-life gift is invisible at the depths we
ship. **Greedy's candidate ORDERING was carrying domain knowledge that substituted for depth, and the
searched path had nothing in its place.**

### The fix
`SubsetHasUnbackedEtbGift` -- same shape as its alt-cost sibling, same `MTG_UNPRUNED` hatch, and the
same SELF-BACKING exemption (an enabler cast in the same subset converts the gift, since the canonical
order resolves the enabler at rank 0 before Aria's ETB at 19). Wired into the same two emission sites.
`MTG_UNBACKED_ETB_GIFT=0` reverts.

### Measured
| arm (Anti-Lifegain, d3+d5) | games | net turns | worse | better | wins lost | losses won |
|---|---|---|---|---|---|---|
| SSM alone, BEFORE the fix | 6000 train | **+31** | 29 | 4 | **7** | 0 |
| **gate alone (greedy kept), train** | 6000 | **-14** | 1 | 13 | 0 | 0 |
| **gate alone (greedy kept), HELD-OUT** | 8000 | **-28** | **0** | **19** | 0 | **5** |
| SSM + gate, train | 6000 | -1 | 14 | 14 | **0** | 0 |

* **The gate is a win on its own** -- zero regressions over 8000 held-out games, 19 improvements, and
  five games converted from a loss to a win. Adopted DEFAULT ON.
* **It REFUTES a claim in the code**: `ArchetypeCardValue`'s comment says "the FIRST pre-Remedy Aria is
  routinely a strictly-better cast (the verse engine)". Over 14000 games it is not -- pruning the cast
  outright is strictly better, and that value was only ever an ordering nudge.
* **Blast radius is ONE CARD.** Aria of Flame is the only card in `cards.json` carrying
  `etb_opponent_lifegain`, and only Anti-Lifegain plays it, so every other deck is byte-identical.
  Levers-off smoke: 0 configs changed, 39 keys unchanged.

### What it does NOT fix: removing greedy is now SAFE, but still not BETTER
SSM + gate is -1 vs the shipped baseline with **zero wins destroyed** (from +31 and seven destroyed
wins), so the catastrophic failure is closed. But SSM+gate vs GATE-ALONE is **+13 turns**: searching
the interior second main still costs 13 turns over 6000 train games once the gift hole is shut.
**MTG_AL_SSM stays OFF.** The remaining 13 turns are the honest measure of what else greedy's ordering
knows that the searched leaf does not -- the next question if the standing "no greedy at any level"
directive is to be finished.

## 2026-08-22y: THE 13-TURN RESIDUAL IS NOT AT THE DECISION -- it is 100% the ROLLOUT'S PLAYOUT
## POLICY. Searching AL's interior m2 DECISION is byte-identical to greedy over 26,000 games.

21x closed with "removing greedy is SAFE but not BETTER: +13 turns, the honest measure of what
greedy's ordering knows that the searched leaf does not." That framing was wrong, and the reason is
that `MTG_AL_SSM` was measuring **two structurally different call sites at once**.
`SolveSecondMainInSearch` is reached from:

| site | caller | what it is |
|---|---|---|
| **BRANCH** | `SolveWithLookahead`'s candidate loop (4 call sites) | a DECISION: the m2 prices "what does passing buy me". This is what the USER directive is about. |
| **ROLLOUT** | `SimulateToEndImpl`, per simulated turn | the playout policy of the LEAF ESTIMATOR. Not a decision: a scoring device. |

`MTG_SSM_SITE` (new, default 0 = both = byte-identical) separates them. The decomposition, AL train
seeds 1001/2002/3003, per game, loss-penalized, greedy-m2 pooled in every batch as a control (its
digests are identical in all three runs):

| arm | d3, 3000 games | d5, 3000 games |
|---|---|---|
| searched at BOTH sites (what 21x measured) | **+12** (12 worse, 1 better) | +1 |
| searched at the **BRANCH site only** | **+0 -- BYTE-IDENTICAL** | **+0 -- BYTE-IDENTICAL** |
| searched at the **ROLLOUT site only** | **+12** (12 worse, 1 better) | +1 |

### The branch site: search and greedy simply AGREE on this deck
Byte-identical play digests across **26,000 games**: 6000 train (d3+d5), 8000 held-out (all four
overnight seeds 4004/5005/6006/7007, d3+d5, every one of the 8 cells digest-identical), and 12,000
across four shuffle salts. Not a dead path -- the searched m2 demonstrably fires (163 real solves +
31 memo hits per 500 held-out games; 5,464 misses per 6000 train games). It is a LOW-EXPOSURE
decision on AL (~0.4-2.4 solves/game), which is why perfect agreement is plausible rather than
suspicious, and that caveat belongs with the result.

**So dropping the last greedy DECISION from Anti-Lifegain's main-phase search is free.**

### The rollout site: real, robust, and not variance
Everything the lever cost lives here, and it is not an artifact of the apparatus:

* **Five shuffle realisations** (`shuffle_salt` 0-4, 30,000 paired games): +12, +7, +9, +13, +7 at
  d3 -- worse in **5 of 5**, never once better in aggregate. Total **+52 turns, 62 worse : 11 better**.
  So it is not draw-order luck, which matters because every one of the four games that survives a
  20x budget is a FETCH-TIMING divergence (different T1 land -> reshuffle -> different draws).
* **Four DECOUPLED search salts** (`shuffle_salt_search` != `shuffle_salt`, 12,000 games): +13, +6,
  +8, +16 -- **+43 turns, 52 worse : 11 better**. So it is not reshuffle clairvoyance either.
* **Budget explains about two thirds of it, not all**: the gap closes +12 -> +7 -> +4 per 3000 games
  at 1x -> 5x -> 20x budget, and the 4 survivors at 20x are exactly the 4 that need +1 or +2 depth.
  The mechanism is plain in the code: the rollout charges the shared budget PER SIMULATED TURN-STEP
  (`SimulateToEndImpl`), and a searched m2 multiplies that charge, so the top-level search's start
  gate skips passes it would otherwise run.
* **It is NON-MONOTONE, which is the real lesson.** Dropping the rollout's m2 *entirely*
  (`MTG_NO_M2_SOLVE=1`) costs **+7 / 3000 at d3, +3 at d5** -- so the interior m2 carries real
  information, and yet searching it costs **+12**. Greedy is an INTERIOR OPTIMUM: both less fidelity
  and more fidelity are worse. More faithful playouts are not more accurate rankings -- which is the
  "HONEST where you SCORE" half of the standing LAW, measured.

### ADOPTED DEFAULT ON 2026-08-22 (USER). `MTG_AL_SSM=0` reverts.
`DecisionProvider::SearchesRolloutSecondMain()` splits the rollout site off, **defaulting to whatever
the deck answered for the branch site** so fivecolour and KittyEquipment are byte-identical.
`AntiLifegainProvider` overrides it to false, and `MTG_AL_SSM` flips default ON. Anti-Lifegain's
main-phase search now takes its interior second-main DECISION by search, with no greedy `Solve`.

The flip is behaviour-neutral TODAY -- its value is structural (no greedy decision remains, and
future AL work cannot silently rest on `EvalCard`'s ordering carrying that decision). Gates, all
against a WORKTREE BUILD OF THE COMMITTED TREE (ed1daedf), never "current binary, flags off":

| gate | result |
|---|---|
| play, 8 AL cells x 3200 games (d3+d5, seeds 1001/2002/3003 + held-out 4004), vs committed tree | **every digest IDENTICAL** |
| **perf**, single-threaded, min of 8 alternating reps per arm | **-0.0% total** (d3 +0.1..+0.6%, d5 -0.2..-1.9%) |
| smoke tier | 39 passed / 0 failed, **0 configs changed** |
| regression tier | 65 passed / 0 failed, **0 configs changed**, 0 slower / 0 faster / 0 play-changed |
| reference reproducibility (--strict, 208 refs) | 0 play-drift, 0 shuffle-dead, 0 enum-gap, 0 mull-drift, 0 contract-fail |
| unit tests | 11 cases / 233 assertions pass |

No GT rebaseline: there is nothing to rebaseline. The perf read is min-of-N for a reason -- a
mid-run rep on this box came back 3x slow on BOTH arms with no other process running (WSL2 host
descheduling), which is the same trap as the "2.1x" contention figure below.

### Cross-deck: the rollout site is NOT generally harmful -- AL is the special case
Measured immediately after adoption (51,200 games, one pooled batch), branch-only vs shipped:
**KittyEquipment is exactly INERT** (0 of 9,600 games differ across train, held-out and three salts,
both depths -- despite the searched m2 firing ~400x per game), and **FiveColour shows no signal**
(+14 turns / 11,500 games, 44 worse : 32 better, p ~ 0.19, rows flipping sign) with its weak
direction the OPPOSITE of AL's. So this is not a case for changing the default anywhere else, and
`SearchesRolloutSecondMain()` correctly stays opt-out rather than becoming the new default. Detail
in `searched-design-deck-rollout.md` §3a.

### Cost: the "2.1x slower" figure from the earlier pass was CONTENTION, not the lever
A multithreaded pooled batch reported ssm_d3 at 140s vs greedy's 67s. Single-threaded on a quiet box
the same cells are **5556ms vs 5661ms at d3 and 5194 vs 5200 at d5 -- the searched m2 is
cost-neutral**. `MTG_M2_SEARCH_DEPTH` remains INERT (digest- AND time-identical at caps 1 and 2), so
the recorded "never binds" verdict stands. Same lesson as 2026-08-19: perf numbers taken under
contention are worthless; `--threads 1` on one job is the only reliable read on this box.

### Instruments added (all default-inert, smoke 39/39 with 0 configs changed)
* `MTG_SSM_SITE` = 0 both (default) | 1 branch only | 2 rollout only.
* `MTG_SSM_BRANCH_ONLY` + `MTG_AL_SSM` as per-job `heurarm` flags -- both arms of the A/B pool into
  ONE batch instead of one batch per arm.
* Per-job `shuffle_salt` / `shuffle_salt_search` manifest fields (`core/GameSetup.h`), which is what
  let a 5-salt ensemble and a 4-salt decouple ensemble each run as a single pooled queue. Previously
  these were env-only, i.e. one process per salt -- the wave pattern CLAUDE.md forbids.
* `scripts/diff_game_lines.py` -- per-turn side-by-side action diff of two single-game logs, for arms
  that differ by a FLAG (test/explain_game.py diffs two BINARIES and does not apply).
