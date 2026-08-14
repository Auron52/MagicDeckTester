# End-of-turn dominance pruning (proposed, measurement staged)

**User proposal (2026-08-14):** drop clearly-dominated states at end-of-turn boundaries inside the
search recursion. Domination axes: life total, cards in hand, creatures on board. The stronger
heuristic form — count a creature ON BOARD as superior to the same card IN HAND — is useful but
"does not fully generalize across decks" (user), so it is a separate, per-deck tier.

## Why now (the measured motivation)

The Class B monster anatomy (gi=17 census, `mirrorwing-gen-perf-profile.md`): a single T1 no-win
decision = 20,355 tree nodes x 50-way branching = 1.02M candidate lines, each leaf-priced by a
~138-step rollout = 140.9M steps. The FSLine memo managed 5.8K hits against that — the lines reach
*distinct* states, so identity-memoization cannot collapse what enumeration created. Most of those
distinct states differ in ways that are strictly WORSE (same board, fewer cards; same hand, less
float), i.e. dominated — enumerated, rolled out, and never able to beat the line that dominates
them. Dominance is the only collapse principle that touches this mass.

## Prior art already in-tree

- `MTG_CANON_SIMKEY` (experiment, default off): memo-key canonicalization — collapses play-order
  PERMUTATIONS of the *identical* position (the x12/ply sequence explosion,
  `th-d5-five-hour-game.md`). Dominance is the next rung: collapse *ordered* (strictly-worse)
  positions, not just equal ones. Canon's caveat applies doubly here: greedy tie-breaks read
  vector order, so neither is provably byte-identical — both are play-affecting changes that need
  the full standing gate.
- Local plan-level dominance folds with the same "never hides a line" argument: sac-for-nothing
  (TurnSolver ~1593), cheapest-j accelerant subsets (~1979), turn-winning-plan-dominates-powerset
  (~2550), Twinflame magnet fold (~3590). This proposal is the same idea applied to STATES instead
  of plans, so it composes with (does not replace) those.

## The sound core (lossless tier)

Two states are comparable ONLY when their futures are identical apart from the compared resources:

- same turn number, same phase boundary (end-of-turn cleanup is the natural hook — "until end of
  turn" effects have expired there);
- **same draws consumed** (same library position). Without this the comparison is unsound: an extra
  draw changes every future.

Then B is dominated by A iff, as multisets/values:

- hand_B SUBSET-OF hand_A;
- board_B SUBSET-OF board_A, and each matched permanent in A is in at-least-as-good state
  (untapped >= tapped; summoning-sick only if B's copy is too);
- **counters compare by per-TYPE monotonicity direction** (user refinement, 2026-08-14): identity
  needs equality, dominance needs only the right inequality per type -- MORE dominates for +1/+1,
  storage charges, depletion charges remaining; FEWER dominates for -1/-1 / other negative counters
  and marked damage. **Vial charge is NOT monotone** (the useful charge tracks the curve: 2 is
  ideal for 2-drops, 5 overshoots it) -- Vial and any other aim-for-a-value counter require
  EQUALITY. Any type without a declared direction: require equality (safe default). The 2026-08-14 storage key-hole find (BuildSimKey
  omitted `storage_counters`, exposed by canon on dragonstorm) is the cautionary tale for this
  table: every counter type IS future-determining, so the dominance comparison must enumerate them
  all -- an omitted type must fail closed (equality), never be silently ignored;
- graveyard_B SUBSET-OF graveyard_A when any deck card reads the graveyard (gy_self_power_bonus,
  retrace, Deathrite fuel) — else graveyards may be ignored;
- **opponent board is a per-deck direction too** (user, 2026-08-14): for nearly every goldfish deck
  it is inert (equality trivially holds — the opponent never gains permanents), but creature_giving
  GIVES the opponent creatures as its drain fuel, so there MORE enemy creatures dominates. Any deck
  that can change the opponent's board needs the axis declared (more-dominates / fewer-dominates /
  equality), defaulting to equality when undeclared — same fail-closed rule as unknown counters;
- floating_B <= floating_A per colour; life_B <= life_A; storm/turn counters equal.

Under goldfish rules every axis above is monotone (more resources never hurt), so dropping B never
drops a strictly-earlier win. It is still NOT byte-identical (a dominated line can win tie-breaks
today), hence: temporary selector flag, A/B on the suite, GT rebaseline if play moves — the
heuristic-optimization route, not a silent switch.

## The heuristic tier (per-deck, opt-in)

The user's "board creature >= same card in hand" rule collapses far more (deploy-order near-misses
become comparable) but is deck-dependent: THIS deck sometimes wants instants IN HAND (a Zada turn
casts from hand; an empty hand is a dead magnet); a discard/madness deck inverts the axis entirely;
and **ETB abilities are the sharpest counterexample** (user, 2026-08-14) — a creature whose
enter-the-battlefield trigger has a payoff condition can be strictly better HELD (cast it when the
payoff is live: more counters to place, a board to pump, a trigger to double), so board >= hand is
wrong exactly when the ETB is why the card is in the deck.

Framing (user, 2026-08-14): the rule works for MOST decks most of the time — even ETB-holding
decks mostly want their permanents played — so it is a very good DEFAULT, and the exceptions are
per-deck configuration, not engine logic. **The opt-in/out decision belongs to the ANALYSIS stage**
(analyze-deck, where the profile/archetype provider is built), recorded as a per-deck profile
flag, correctable later if analysis got it wrong. Examples: treasure_hunt opts OUT of the
play-a-land dominance (its lands-in-hand are retrace ammo — Flame Jab); "play a land each turn you
have one" genuinely dominates for nearly every other deck. The analysis stage can auto-suggest the
setting from the list itself (retrace / discard costs / madness / conditional-ETB payoffs are the
opt-out signals), with the user confirming. **The flag itself lives in the HEURISTIC (archetype)
provider** — the analysis stage picks its value, the provider carries it — never root engine truth;
measured A/B settles disputed cases.

## Instants/sorceries: dominance keeps them in hand (falls out of the sound core)

For a nonpermanent spell the preference INVERTS (user, 2026-08-14): a line that CAST it and
changed nothing else is dominated by the line that held it — hand-with-spell is a superset, the
rest equal, so the sound core already prunes it wherever the graveyard axis is ignorable (no
retrace/gy-payoff). Normally a resolved spell changes something, so this tier only removes WASTED
casts — fizzled tricks, pumps on nothing, the magnetless Gold-Rush-as-ritual lines from the
2026-08-14 branching investigation (cast {1}{G}, crack the Treasure, end with less of everything).

**Layering rule (user, 2026-08-14): dominance SUBSUMES the plan-level waste folds correctness-wise
but does NOT replace them.** A plan-level fold (SubsetWastesAccelerant, sac-for-nothing, the
existing ritual-waste prunes) removes the line BEFORE it is enumerated/applied/simulated — zero
budget spent; EOT dominance catches the same line only after paying its whole turn of work. Keep
every cheap early fold; dominance is the general backstop for the waste no plan-level rule
anticipated.

## Implementation sketch

A per-decision Pareto archive keyed by (turn, draws-consumed), sitting next to FSLineCache:
check-and-insert at the NewTurn transition in the FSLineWin recursion. Insert returns
{dominated -> prune subtree, dominates -> evict archived entries, incomparable -> keep}. Archive
size is the Pareto-frontier width — bound it (keep newest K per key) so a wide frontier degrades to
today's behaviour rather than blowing memory. The subset comparisons need the canonical multiset
form — CANON_SIMKEY's sorted (h1,h2) fold is the right substrate, one more reason to measure it
first.

## Measurement plan (before any adoption)

1. `MTG_CANON_SIMKEY=1` on the gi=17 monster (zero new code; running 2026-08-14): how much of the
   1.02M is order-permutation identity, and does the answer change?
2. Dominance census probe (MTG_BP_DUP_PROBE precedent — temporary, stripped after): at each
   end-of-turn boundary, bucket sibling states as {identical, dominated, incomparable} under the
   sound core. This prices the lossless tier before it is built.
3. If built: A/B win-turn + cost on the regression suite train seeds, validate on held-out, full
   standing gate (play-affecting). Measure the heuristic tier separately, per deck.
4. **MUST-FIND gate (user, 2026-08-14, from the canon adoption):** dominance is a JUDGMENT prune
   (its failure mode is a reachable win silently dropped — no per-state validity argument exists,
   unlike identity collapse), so it additionally passes the unbounded-budget reproduction test:
   every previously-found win in the gate sweep must be found at `--budget-ms 0` with dominance ON,
   or the prune is wrong — the only exemption is a win outside the search window from every
   diverging decision. Prefer the SIBLING-FRONTIER application point first (same decision, same
   consumed draws — comparability by construction); cross-decision archives only after that form
   is proven.

## Sequencing

Independent of the search/play mismatch fix (`mirrorwing-search-play-mismatch.md`) and of the
keepgen/bottoming work — attacks the same monsters through a third door (state count, vs early
exit and leaf pricing). Composes with the Expedite / Scale-the-Heights group folds the 2026-08-14
branch-stats probe surfaced.
