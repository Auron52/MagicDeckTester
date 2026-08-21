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
