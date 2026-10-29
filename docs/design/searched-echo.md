# Searched echo (MTG_ECHO_AXIS) -- 2026-10-29

Echo ("At the beginning of your upkeep, if this came under your control since your last upkeep,
sacrifice it unless you pay its echo cost", CR 702.29) used to be decided by the provider
(`DecisionProvider::PayEchoToKeep`, Goblins override `GoblinsProvider::PayEchoToKeep`) at two places,
neither of which branched:

* the executor's real upkeep (`AIEngine::ResolveEchoUpkeep`, from `GameEngine::UpkeepTail`), before
  that turn's search starts;
* the rollout's turn boundary (`SimulateEndAndStartNextTurn`), which `FSLineTail` crosses with depth
  remaining.

That is an in-window greedy pick (CLAUDE.md, NO GREEDY PICK INSIDE THE SEARCH WINDOW). A code comment
claimed "the search need not branch echo". It was wrong. The placeholder-card audit fixed the provider's
hand-cost read, and the "no gas -> pay" clause started to fire. It then measured Goblins games a turn
slower that did not recover at `--depth 8 --budget-ms 0`. In each of them the right answer depends on the
turn the upkeep opens: its draw and its land drop, which the upkeep-time judgement never sees. In seed
1350001 gi640, Goblin Lackey put Muxus in T2 combat and Muxus put Mogg War Marshal. At T3 the echo was
paid with two of the three lands, and the T3 draw was the Mountain that would have cast Goblin Chieftain
for the T3 kill.

## The design

**The pin.** The plan of the turn before carries the answer: `Plan::echo_choice` ->
`GameState::scripted_echo`, the Emeria / Search-for-Tomorrow next-upkeep state-pin shape.
`TurnSolver::WritePostApplyStatePins` writes it in both worlds, write-when-set. The next upkeep consumes
and clears it, again in both worlds: `AIEngine::ResolveEchoUpkeep` plus `GameEngine::UpkeepTail`'s
unconditional clear on the executor side, the rollout's echo block on the other. Both loops read it
through one function, `EchoUpkeepWantsPay` (`core/SpellEffects.h`).

**Relative to the provider.** The encoding is `-1` for no pin. Otherwise bits 0..7 are an INVERT mask by
obligation ordinal, where an obligation's ordinal is its position in battlefield order among the active
player's permanents with an unresolved echo. Bits 8..11 count how many ordinals the pin COVERS.

* A covered ordinal whose bit is clear follows the provider. One whose bit is set does the opposite.
* An ordinal past the cover takes the provider's answer.

It is relative because the provider's answer is a function of the UPKEEP state, which no plan can see when
it is enumerated. That gives two properties:

* The base plan IS the provider's answer, so the provider orders the branches (base first, ties keep it).
* The clone is the other answer, so the two never coincide and no duplicate branch is ever emitted.

**Where it fans.** `AppendEchoAxisRange` runs at the top of `AppendSubdecisionAxes`, before every other
axis. It is multiplicative: each echo world then gets every other axis's variants, with its own fold
identities.

* A plan whose own hand casts / Vial puts plus the obligations already pending reach past the state's
  cover extends the cover. The base is stamped "follow" on the new ordinals. One clone is emitted per
  non-empty inverted subset.
* So main 1 decides what main 1 brings, and main 2 decides only what arrived since: a Goblin Lackey put, a
  Muxus reveal, a main-2 cast. Nothing is decided twice.
* A wave-0 breakpoint variant is stamped "follow" and not cloned. Its inverted world's continuations come
  from the deferred-wave walker over the clone, whose `bp_wave0` is cleared like every axis clone's.
* The hosts that append an idle plan after the axes ran fan it too (`AppendIdleEchoVariants`): FSLineTail's
  do-nothing second main, SolveWithLookahead's empty post-combat set, the NC instrument's bare post-combat
  list.
* A second main is never "unproductive" while an uncovered obligation is pending (`SecondMainUnproductive`
  -> `EchoUncoveredPending`), so neither the 0-cost empty path nor the interior solve's early return can
  skip the decision.

**The boundary fan, for obligations no enumeration could see.** An obligation the APPLY itself creates did
not exist when any plan was listed:

* a breakpoint continuation's cast;
* the cast a Goblin Matron tutor feeds through the canon continuation;
* a Muxus reveal.

On the census sample, before this fan, they were 6,184 of the 194,658 obligations the search resolved inside
the window (3.2%), every one in the last main of the turn. FSLineTail decides them where it crosses the boundary (`EchoBoundaryPins`).
There is one crossing per inverted subset of the uncovered ordinals, with "follow" first so it wins ties. The
winning pin is recorded on the line's plan (`Plan::echo_choice`), so the executor writes the same pin at that
plan's apply.

It runs at FSLineTail's three second-main crossings: the plan loop, its breakpoint-node children and its
deferred continuation waves. Two crossings are not fanned:

* the condemned-tranche audit loop (`MTG_CONDEMN_TRANCHE`, default OFF);
* the single-main tail. No deck with an echo card is single-main: Goblin Lackey makes Goblins search a second
  main every turn. The fix, if one ever is, is a `SearchLine` field carrying the chosen pin back to the
  caller's main-1 plan.

**The edge fold (a sound identity fold).** A full-line node whose next turn is a CONSTANT leaf skips the axis.
That covers the no-leaf stand-in Goblins ships (`value_play.leaf "none"`) and a forced constant leaf
(`EchoEdgeFoldHere`, `FsNextLeafConstant`). The leaf answers max_turns+1 for every state, so the next upkeep's
echo answer cannot move any value that node computes. Both pins would score alike, and "follow" is what
ordering picks anyway. A deeper pass searches that turn, and there the echo is branched, so no line becomes
unfindable at any budget. A pass whose leaf is the real rollout (the hybrid escalation's forced heuristic
leaf) fans as usual.

**Where it does not fan.**

* Inside a breakpoint continuation derivation: the pin is the OUTER plan's, consumed after its apply.
* In human play: the person answers the upkeep prompt (`m_external_echo_chooser`).
* In a narrowed tail rollout (`t_rollout_narrow`): beyond the horizon the provider's pick is the permitted
  playout policy, and the greedy playout's plans carry no pin.
* The d0 runner (`Solve`) never enumerates axes, so its upkeeps take the provider's pick (the D0Runner
  permit).

**Gating.** `GameState::deck_has_echo` is a deck constant stamped by `StampDeckTraits`, so every deck
without an echo card is byte-identical by construction. `MTG_ECHO_AXIS=0` (heurarm slot `ECHO_AXIS`) is an
A/B arm only, and play-identical to the pre-change tree. Both are measured below, under Byte-identity.

**Keys.** The pin is future-determining. It is folded value-gated into:

* `dominance::Build()`;
* `BuildDedupKey`'s pending pins;
* `BuildBreakpointKey`. The axis reads the inherited cover, so the m1/m2 enumeration memos and the
  searched-m2 memo must tell two pins apart.

`Plan::echo_choice` is in the plan signature, hash, equality, `IsApplyEmptyPlan`, the axes key, the
executor's re-solve pin checks (`CarriesPostApplyPin` / `RecordReSolveRealized`) and the provider route
signature. `sizeof(GameState)` and `sizeof(Plan)` are unchanged: both fields sit in existing padding,
measured with gdb `ptype /o`.

**The census (`MTG_ECHO_AXIS_STATS=1`).** It counts every obligation the upkeep loops resolve:

* covered or inverted;
* uncovered while the innermost search frame had depth (`greedywindow::InWindow`). This is the no-greedy
  invariant's instrument, the madness counter's twin.
* the executor's real upkeeps in a searched game that no plan covered.

## The lockstep contract, and what relative pins do not buy

The executor plays exactly what the search planned **as long as `PayEchoToKeep` answers the same in both
worlds on the same upkeep**. That is the contract every provider hook called in both worlds already
carries, and the axis does not weaken it.

What it does not buy is immunity to a provider that breaks the contract. Measured: gi1042 with
`MTG_ECHO_STAGED_GAS=0` puts the impulse card in hand in the rollout and out of hand in the executor, the
split 962d538dd fixed. That game is still T5 with the axis on, at play settings and at d8 b0, because
"follow" means different answers in the two worlds. With the fix on (the default) it is T4 either way.

An ABSOLUTE encoding (pay / decline per ordinal) would make the executor immune. It would also lose the
provider's exact ordering: the base would have to be picked from a proxy at enumeration time, and ties
would stop keeping the historical answer. That choice is open for the USER (see the last section).

## Cumulative upkeep (Varchild's War-Riders): kept as a one-option prune, with the proof

`PaysCumulativeUpkeep` (default true, never overridden) is picked the same way at the same sites. It is
NOT branched. Paying is weakly dominant against the passive goldfish for the only deck that holds the
card, Creature Giving. Checked against `cards.json`, card by card:

* **Paying** keeps the 3/4 trample attacker and gives the opponent N 1/1 Survivors. Every card in the list
  that reads an opposing creature only gains from them:
  * Suture Priest drains 1 per entering opposing creature;
  * Defense of the Heart needs three;
  * Massacre Wurm drains 2 per dying one;
  * Soul / Essence Warden gain life.
* The goldfish opponent never attacks and never blocks, so the gifts cannot hurt.
* **Declining** sacrifices Varchild, and nothing in the list pays off our own creature dying. There is no
  own-death watcher and no sacrifice outlet; Crop Rotation sacrifices a land.

So the decline branch can never beat the pay branch. Branching it would double Creature Giving's search
on every turn Varchild is on the battlefield, for nothing. That is wasted search, collapsed per CLAUDE.md.

The proof breaks if a deck holding the card also holds any of these:

* a payoff for its own creatures dying (Blood Artist, a sacrifice outlet);
* anything that reads the opponent's creature count against us;
* a real opponent (1v1).

**Next item when that happens:** generalise the pin. Cumulative-upkeep obligations become ordinals after
the echo ones, gated by a provider predicate "decline can beat pay here" (default false = this prune).
The pin, the consumers and the census need nothing else.

## Survey: every upkeep / turn-boundary provider pick reached with depth remaining (2026-10-29)

`SimulateEndAndStartNextTurn` and its helpers, with each executor twin. "Searched" means a plan state pin
whose axis is live by default.

| # | Decision (site) | Hook / rule | Searched? | Decks |
|---|---|---|---|---|
| 1 | Abiding Grace end-step mode + target (`PerformEndStepAbidingGrace`) | `GraceOptions` front | YES, `scripted_grace_choice`, `MTG_GRACE_AXIS` ON | LifegainHeliodCombo |
| 2 | Cleanup discard (`CleanupDiscardShed`) | `CleanupDiscardCandidates` front | Pin exists (`scripted_discard_choice`), but width = `CleanupDiscardSearchWidth()` = 1 for every provider by default (Treasure Hunt's `MTG_TH_DISCARD_WIDTH` = 1), so effectively NOT searched | any deck over 7 cards |
| 3 | Madness on a cleanup shed (`ResolvePendingMadness`, Cleanup window) | `MadnessCastDefault` (opts[0]) | NO. Documented as a dominance fold (USER question 4), excluded from the madness in-window counter | Vampires |
| 4 | Search for Tomorrow off suspend (`CastOffSuspend`) | land pick front | YES, `scripted_upkeep_land_search`, no kill switch | Hydras, Landfall |
| 5 | Aether Vial charge | `WantVialCharge` | Pin exists (`scripted_vial_charge`); `MTG_VIAL_AXIS` default OFF (USER 2026-08-30: "in general we don't want to fully search vial decisions") | Goblins, Knights, LandfallElementals, Minotaur, Pirates, Soldiers, slivers_vial, Vampires, WhiteKnights |
| 6 | Varchild cumulative upkeep (`PerformUpkeepCumulativeGifts`) | `PaysCumulativeUpkeep` = true | NO, dominance-safe one-option prune (above) | Creature Giving |
| 7 | Defense of the Heart put list (`PerformUpkeepSacTutor`) | `SacTutorPutList` (closed-form immediate-drain maximisation; subset AND enter order) | **NO** -- a genuine multi-option pick, not shown dominated. Next item | Creature Giving |
| 8 | Emeria upkeep return (`PerformUpkeepGyReanimate`) | list front / decline | YES, `scripted_upkeep_gy_reanimate`, `MTG_EMERIA_AXIS` ON | LifegainHeliodCombo |
| 9 | Mirri's Guile top-3 order (`PerformUpkeepReorder`) | `HeuristicTopDisposition` | **NO** (an ordering pick) -- no current deck holds the card (only archived StompySurprise/v1) | none current |
| 10 | **Echo** | `PayEchoToKeep` | **YES, this change** (`scripted_echo`, `MTG_ECHO_AXIS` ON) | Goblins, goblins2hg |
| 11 | Upkeep Call of the Wild (`ResolveUpkeepRevealTop`) | `ActivateRevealTopAtUpkeep` = true | NO; the whole block is behind `MTG_UPKEEP_CALL` (default OFF), and it has no human chooser yet | StompySurprise |
| 12 | Saga chapter II/III target (`AdvanceSagas`) | `SagaChapterTargetCandidates` front | YES, `scripted_saga_target`, `MTG_SAGA_TARGET_AXIS` ON | StompySurprise |
| 13 | Saga chapter I free cast at the boundary (`DrainPendingSagaEnters` safety net) | trial-on-a-copy heuristic | NO at the boundary (normally empty there) | StompySurprise |
| 14 | ETB K-choices of creatures PUT at upkeep (DotH / Grace / Emeria / Call of the Wild puts: `FireOwnEtbTriggers` with `kEtbKxHeuristic`, e.g. Terastodon's `EtbDestroyK`) | provider one-option rules | NO (provider prunes; Terastodon's is a USER-approved one-option heuristic) | StompySurprise, Creature Giving, LifegainHeliodCombo |
| 15 | Engine-fixed trigger order (Asylum Visitor before the Vial; Varchild before DotH; Ocelot before Pyrohemia; Grace last) | fixed | NO (ordering) | Vampires, Creature Giving, Fungus |

Executor-only: **Dwarven Hold's storage hold** (`GameEngine::UpkeepTail` -> `StorageLandHold`). The
rollout never asks. That is lockstep today only because the base returns false and nothing overrides it.
Any override would split the two worlds (Dragonstorm).

**Next items, in order of live impact:** #7 (Defense of the Heart's put list, a live multi-option pick
in Creature Giving), #2 (the cleanup-discard axis is wired but its width is 1 everywhere), #5 (the Vial
axis is a USER decision, default OFF), #9 / #11 / #13 (no current deck, or behind an off lever), and the
Dwarven Hold lockstep hazard.

## Measurement

Base: 513e27555, the placeholder split, on 67e4d62e6. `MTG_PLACEHOLDER_ECHO` is default OFF, so the
provider's echo read is the placeholder one unless that lever is set.

Every run is one pooled batch, `--threads 12`, `MTG_PGO=0`, `MTG_RSS_CAP_GB=43`, at play settings (the
deck's `value_play`: d7 b60), with the arms pinned per job. The seeds are new and held out:

* Goblins: 1540001 and 1550001, 4,000 games each;
* goblins2hg: 1560001, 4,000 games (30 life, two heads).

| arm vs E0 (axis off = the committed tree) | deck | games | faster | slower | net turns | z | units |
|---|---|---|---|---|---|---|---|
| **E1** axis on | Goblins | 8,000 | 5 | **0** | **-5** | -2.24 | 1.022x |
| **E1** axis on | goblins2hg | 4,000 | 4 | **0** | **-4** | -2.00 | 1.014x |
| E1PE axis on + `MTG_PLACEHOLDER_ECHO=1` | Goblins | 8,000 | 5 | 0 | -5 | -2.24 | 1.022x |
| E1PE axis on + `MTG_PLACEHOLDER_ECHO=1` | goblins2hg | 4,000 | 4 | 0 | -4 | -2.00 | 1.014x |
| E0PE axis off + `MTG_PLACEHOLDER_ECHO=1` | Goblins | 8,000 | 1 | 7 | +6 | +2.12 | 1.002x |
| E0PE axis off + `MTG_PLACEHOLDER_ECHO=1` | goblins2hg | 4,000 | 0 | 5 | +5 | +2.24 | 0.999x |

Notes on the table:

* **E1PE vs E1:** 0 faster / 0 slower in all 12,000 games, with 61 + 51 games' play changed. Once echo is
  searched, correcting the provider's cost read cannot cost a game: it only reorders the branches.
* **Wall clock:** core-seconds were E1 0.98x (Goblins) and 0.98x (2HG) on a shared box, within noise.
* **`[fd-diverge]`** (`MTG_FD_ORACLE=1`): 0 in every arm.
* **Units before the edge fold:** the first build had neither the edge fold nor the boundary fan. It ran on
  the old base 3b0abb513, where the definition cost read was on by default, over seeds 1470001 / 1480001 /
  1490001 (Goblins 8,000, goblins2hg 4,000).
  * It was -8 (8 faster / 0 slower) and -9 (10 / 1).
  * It cost 1.072x / 1.075x units. The edge fold removed most of that.

**Slower games:** none in E1 or E1PE. So nothing needed a d8 b0 check on the final build.

The first build, on the old base 3b0abb513, had one slower game: goblins2hg 1490001 gi3150, T3 -> T4 at
play. It recovered at d8 b0 (T3). It was a budget dilution: the extra branches pushed the T3 line out of
the b60 budget.

**The witness games (final build), d8 b0 / play:**

| game | E0 | E0PE (the failing read) | E1 | E1PE |
|---|---|---|---|---|
| s1350001 gi640 | T3 / T3 | T4 / T4 | T3 / T3 | **T3 / T3** |
| s1350001 gi1042 | T4 / T4 | T4 / T4 | T4 / T4 | T4 / T4 |
| s1360001 gi405 | T4 / T4 | T5 / T5 | T4 / T4 | **T4 / T4** |
| s1360001 gi3725 | T3 / T3 | T4 / T4 | T3 / T3 | **T3 / T3** |

gi1042 was slower only before 962d538dd (the staged-card scan); it is T4 in every arm on this base.

**The cover census** (`MTG_ECHO_AXIS_STATS=1`, Goblins 400 + goblins2hg 200 games, final build):

| obligations the upkeep loops resolved | 120,256 |
|---|---|
| covered by a searched pin | 20,462 (10,130 inverted) |
| at a constant-leaf edge (the identity fold) | 95,844 |
| in the playout beyond the horizon (permitted) | 3,818 + 132 with no frame |
| **uncovered inside a searched frame** | **0** |
| the executor's real upkeeps in a searched game | 144, **0 uncovered** |

Before the boundary fan, the census counted 6,184 uncovered inside a searched frame on the same sample:
continuation casts, a Matron-fed cast and Muxus reveals in main 2.

**Byte-identity:**

* `MTG_ECHO_AXIS=0` is play-identical to the pre-change binary. Measured on the first build's base
  (Goblins + Creature Giving, 16/16 jobs, units 1.000x). Every path added since is gated on the same
  predicate.
* With the axis on, Creature Giving and creature_giving2hg are identical to the pre-change binary: 200 +
  100 games on the first base, and on this base 100 + 100 games axis-on vs axis-off, 8/8 jobs identical in
  digest and units. They hold no echo card, so `deck_has_echo` keeps every walk off.

**The suite's Goblins rows** (smoke 1001; regression 2002 / 3003; the unbudgeted backstop 30001), axis on
vs off on the final build:

* every row's average is identical;
* every digest is identical except regression goblins d5 s3003: one game's play changed, same win turn;
* units range from 1.006x to 1.046x.

So the smoke ground truth does not move, and the regression GT moves by that one digest.

## Open for the USER

1. **`MTG_PLACEHOLDER_ECHO` can turn on.** Its stated block was this change. With the axis on it measured
   0 better / 0 worse in 12,000 held-out games, and it recovers the three witness games. Whether to flip
   it, together with `MTG_GOBLIN_ECHO_LAND_DROP` / `_DRAW_LAND`, is the USER's call. So is whether the
   Goblins keep rule (lethal / no-gas, `GoblinsProvider::PayEchoToKeep`) is still wanted now that it only
   orders.
2. **Relative vs absolute echo pins** (the lockstep section above). Relative keeps the provider's exact
   ordering and inherits the provider's both-worlds contract. Absolute is immune to a provider split but
   orders by a proxy. Recommendation: keep relative, since the one known split is fixed (962d538dd).
3. **Next items** from the survey: Defense of the Heart's put list first.
