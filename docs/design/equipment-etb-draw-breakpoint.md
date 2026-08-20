# The equipment-ETB draw was drawn but never decided (breakpoint site 6)

> **STATUS 2026-08-20** — implemented behind `MTG_EQUIP_DRAW_BP` (default OFF), measured over 2764
> games in two pooled batches, **awaiting the user's adoption call**. The play win is real,
> reproduces on held-out seeds and never loses a game; it costs ~1.85-1.99x the search work on this
> deck (and nothing anywhere else); the one sanctioned cost lever
> (`MTG_EQUIP_DRAW_BP_DEFER`, a visit-ORDER change, not a prune) is **measured and refuted**. The
> un-levered game-log fix in the same work is not part of that call — a draw that happened must be
> reported — and is inert for every deck in the tiers (smoke 36/36, 0 configs changed).

## The defect

Puresteel Paladin reads "Whenever an Equipment you control enters, you may draw a card." The draw is
fully implemented, and it fires in **both** worlds — it lives in `OnDragonEnters`, the universal
enter cascade, so it happens for a cast Equipment and for one put onto the battlefield by Stoneforge
Mystic or Armored Skyhunter alike (`SpellEffects.h`), and `ApplyPlanDirect` calls that same cascade
for a noncreature permanent (`TurnSolver.cpp`) precisely so the rollout's library does not run ahead
of the real game.

What never existed is the **decision that follows the draw**. Every other draw engine in the codebase
arms a breakpoint: the phase re-solves from the post-draw state so the revealed card can be cast with
the mana this turn has left. This one armed nothing, so the drawn card sat in hand until the next
turn no matter how much mana was open.

Three independent checks, the third decisive:

* `MTG_BP_PROBE` was **silent** on this deck — at d3 and on the d5 tail alike, KittyEquipment hit
  zero breakpoints of any class.
* Over the 150 logged held-out games: ~109 main-1 equipment-ETB draws across 56 of them, and a card
  that was *not* already in hand at the start of the turn was cast in main 1 **exactly zero times**
  (the natural draw excluded via the logged DRAW-phase action).
* `git log -S draw_on_equipment_etb -- src/ai/AIEngine.cpp src/ai/TurnSolver.cpp` returns **nothing**,
  and no commit message discusses an equipment-draw breakpoint. So unlike the plain-cantrip class
  (bit 3, an *admitted* quality prune with measurements behind it), this was never a decision at all.
  The card was implemented during a later deck onboarding and simply never joined the classifier.

## Why it was structurally unreachable

Every existing classifier keys on a **card param of the resolving spell**:

| classifier | file | keyed on |
|---|---|---|
| `note_draw_engine` | `AIEngine.cpp` | `d->params.*` of the cast |
| `is_draw_engine` | `AIEngine.cpp` | `d->params.*` of the cast |
| `OrderingOpaque` | `ManaPayment.cpp` | name -> params of the cast |
| `PlanOpensBreakpoint` | `TurnSolver.cpp` | `a.def->params.*` of each action |
| `is_draw_spell` (log) | `GameEngine.cpp` | `def->params.*` of the resolving spell |

No param on Bone Saw says "this draws", because **the draw belongs to the watcher, not to the
equipment**. A Bone Saw cast draws a card if and only if a Puresteel Paladin is already on the
battlefield. So the classification has to read the board, and none of these could express that.

That is the general lesson worth carrying: a param-keyed classifier is blind to any effect owned by a
*permanent already in play*. Any other "whenever X enters/is cast, do Y" watcher added later will be
invisible to all five of the tables above in exactly the same way.

The fix is one shared state-keyed predicate, `TurnSolver::EquipmentEtbDrawFires(state, def)`, plus a
levered wrapper `EquipmentDrawBreakpoint` used by the search. Both worlds read the *same* function,
which is what keeps them from disagreeing about whether a breakpoint exists — the lockstep failure
`BpSiteMask`'s comment describes (a committed continuation replayed at the wrong index).

## The class: site 6, deferred

`BpSiteMask` grows a seventh bit (default `0x37` -> `0x77`), and the class is **deferred** to after
every main cast, like the trick class (site 5) and unlike the inline draw-spell classes:

* **The deck casts several Equipment in one main phase**, each drawing. Deferring gives one
  breakpoint per phase, with every drawn card in hand and the remaining mana known, instead of N
  nested re-solves of which only the first is searchable.
* **A nested greedy re-solve mid-continuation taps mana the plan's own later casts need** — the
  mirrorwing gi52 defect that moved the trick class off the inline path.
* **It lands the two worlds at the same point in the turn.** The rollout's deferred re-solve runs
  after the trailing Equip / Stoneforge / Balan passes, and so does the executor's catch-all
  committed replay (`fd_plan_committed && !bp_replayed`, `AIEngine.cpp`). Equip costs move with
  metalcraft, so a continuation that ran *before* those passes on one side and after on the other
  would price them differently. The inline hook (`is_draw_engine`) fires at the cast, i.e. before —
  which is why site 6 is deliberately **not** added to it.

Executor arming is therefore one clause in `note_draw_engine` (the depth-0 second pass); depth > 0
replays through the existing catch-all. This inherits the known asymmetry that sites 3 and 5 already
have: on a **non-committed** depth>0 turn no deferred-class continuation is realised, because the
deferred classes are absent from `is_draw_engine` and the catch-all is gated on `fd_plan_committed`.
That is pre-existing and shared, not introduced here.

`PlanOpensBreakpoint` takes the state now, and its site-6 clause counts a watcher that is **on the
battlefield or cast by this same plan** — the deck's reviewed cast order puts Puresteel Paladin ahead
of the equipment precisely so its draws happen, so missing that case would leave the deck's best
turns (Paladin + two Equipment) permanently greedy while fanning out the lesser ones.

## Measurement

One pooled batch, 12 jobs / 1264 games, four arms in a single queue via per-job `flags`
(`test/tools/kitty_ab/gen_equipdraw_manifest.py`). Four arms rather than two because the metalcraft
credit (`MTG_METALCRAFT_CREDIT`, also pending adoption) acts on the same turns — the credit is what
makes a second Equipment castable, and this breakpoint is what lets the card those Equipment *drew*
be spent — and this repo has measured a lever at +0.0201 alone and -0.0616 in combination.

Paired per game (`compare.py`); delta < 0 is faster, the improvement direction:

| cell | arm | delta | se | t | faster | slower | plays differ |
|---|---|---|---|---|---|---|---|
| hold (900001, d3, 150) | bp | **-0.0267** | 0.0132 | -2.02 | 4 | **0** | 18 |
| hold | mc | -0.1400 | 0.0300 | -4.67 | 22 | 1 | 58 |
| hold | mc+bp | -0.1533 | 0.0310 | -4.95 | 24 | 1 | 62 |
| train (300001, d3, 150) | bp | **-0.0200** | 0.0115 | -1.74 | 3 | **0** | 23 |
| train | mc | -0.1133 | 0.0260 | -4.36 | 17 | 0 | 51 |
| train | mc+bp | -0.1200 | 0.0266 | -4.51 | 18 | 0 | 61 |
| repro (70001, d5, 16) | bp | 0.0000 | 0.0000 | — | 0 | 0 | 2 |
| repro | mc | -0.2500 | 0.1118 | -2.24 | 4 | 0 | 5 |

Train and held-out agree in sign and magnitude, and the marginal contribution on top of the credit
(`mc` as baseline) is -0.0133, 2 faster / 0 slower — the two overlap, as expected, but neither
cancels the other. `mc` reproduces its own earlier held-out -0.1400 exactly, which is the sanity
check that the harness is measuring what it did before. The 16-game d5 repro cell is too small to
resolve an effect this size and says nothing either way (it moved play in 2 games and no turn).

**Zero games got slower in any comparison**, and `MTG_FD_ORACLE` reported zero `[fd-diverge]` lines
across all arms: the committed lines the search proves are the ones the executor realises.

### The cost, which is the problem

Paired work units (`cost.py`, `GameWorkMeter` — deterministic, so load cannot distort it; batch
per-game ms on this box is wall, not CPU, and has read 16.5 s and 48.9 s for the same workload):

| cell | arm | total units vs base | mean per-game ratio | cheaper | dearer |
|---|---|---|---|---|---|
| hold | bp | **+78.97%** | 1.906 | 3 | 92 |
| hold | mc | -15.22% | 0.915 | 47 | 42 |
| hold | mc+bp | +32.88% | 1.495 | 29 | 68 |
| train | bp | **+248.38%** | 1.849 | 2 | 92 |

(`mc` is *cheaper* because it wins sooner — fewer turns to search.)

The cause is structural, not a defect: wave 0 emits `W=2` variants per breakpoint-opening base plan,
and site 6 fires on almost every plan this deck has, so the candidate set roughly **triples at every
node**. Every other class fires on the handful of plans holding one particular card; this one is
close to universal.

## The cost reduction: measured and REFUTED

`MTG_EQUIP_DRAW_BP_DEFER` drops site 6 from `BpWave0SiteMask` only. Per the user rule
(2026-08-19) — *"the re-ordering of when we visit nodes can be workable, but skipping them entirely
without being certain they don't hold an earliest win is not"* — this is the sanctioned move and not
a prune: `BpWaveWalker` is built off the **full** `BpSiteMask`, so every dropped plan is picked up by
the deferred wave phase at rank 0 with whatever budget wave 0 left. No continuation becomes
unreachable; only the order in which the search reaches them changes.

It does not work. Second pooled batch, 5 arms / 1500 games (`gen_equipdraw_defer_manifest.py`,
`logs/kitty_edbp2`), re-running `base`/`mc`/`mc_bp` inside the same binary so every pairing is
self-contained:

| cell | metric | eager (`bp`) | deferred (`bpdef`) |
|---|---|---|---|
| hold | delta / faster / slower | -0.0267 / 4 / 0 | **-0.0267 / 4 / 0** |
| train | delta / faster / slower | -0.0200 / 3 / 0 | **-0.0200 / 3 / 0** |
| hold | units vs base | +78.97% | **+89.13%** |
| train | units vs base | +248.38% | **+318.39%** |

Quality is identical (149 of 150 games byte-identical between `mc_bp` and `mc_bpdef` on hold) and the
cost is *worse*. The mechanism is working exactly as designed and that is the reason: the deferred
wave phase picks these plans straight back up at rank 0, so the same continuations get scored either
way and the deferral buys only bookkeeping.

**The generalisable point for any future attempt on this site**: the expense is not *when* wave 0
fans out, it is that site 6 fires on nearly every plan the deck has, so `W=2` roughly triples the
candidate set at every node. Re-ordering cannot fix a multiplier that applies everywhere. The lever
is kept in the tree as the record of the refutation, in the same spirit as `EquipmentProvider`'s
rejected `EnumGroupCap`.

Round 2 also reproduced round 1's `base`, `mc` and `mc_bp` numbers exactly, which is the check that
the interim game-log fix was inert.

## The open lead: the search believes it, the executor rarely does it

The census that found the bug, re-run on the fix (`drawn_card_used.py`, 24 games, seed 900001, d3 —
now a direct measurement rather than an indirect one, because the log fix below makes the draw
visible):

| config | mid-main draws | spent in the same phase |
|---|---|---|
| baseline | 15 | **0** |
| `MTG_EQUIP_DRAW_BP` | 15 | 1 |
| `MTG_METALCRAFT_CREDIT` | 14 | 0 |
| both | 14 | 0 |

So the realised rate of the thing the class exists to enable — spending the drawn card in the phase
that drew it — is **1 in 29**. The measured win is therefore mostly the ROLLOUT modelling the turn
better (it now knows an Equipment cast yields a card, which changes which plan it commits), not the
executor spending the card.

That is not because the continuations are empty. `MTG_BP_CANDS_PROBE` over 57,793 site-6 breakpoints:
**mean 9.47 continuations, max 146**, only 12% of them length-1, **74.1% capped at `W=2`, and 80.1%
of all continuations rank-unreachable** in wave 0. There is a lot of choice at these breakpoints and
the search is reaching a fifth of it.

Two candidate causes for the realisation gap, in order of suspicion, both cheap to test next:

1. **The `fd_plan_committed` gate.** On a non-committed depth>0 turn no deferred-class continuation
   is realised at all (see the class section above). This is pre-existing and shared with sites 3
   and 5 — but this deck may hit it far more often, and if so the class is worth several times what
   it currently measures. Test: count committed vs non-committed turns against realised
   continuations.
2. **The trailing equip pass spends the mana first.** The deferred re-solve runs *after* the Equip /
   Stoneforge / Balan passes, which is what keeps the two worlds aligned — but those passes may leave
   nothing to cast with. The census with the metalcraft credit on (equips at {0}) does not obviously
   contradict this, since it went 0 of 14, so it needs a direct read rather than an inference.

Neither is a reason to hold the class; both are reasons its current -0.025 is probably a floor.

## Also fixed: the log never reported the draw

`GameEngine::ResolveStack`'s draw reporter has the identical blind spot (`def->params.draw > 0` and
friends), so a Puresteel draw put a card in hand with **no `DRAW` action ever logged** — the play
viewer could not show where it came from, and a log-based census could only find it indirectly, which
is why the census above had to be phrased as "cast a card that was not in hand at the start of the
turn". Fixed with the **un-levered** predicate: the draw happens whichever way `MTG_EQUIP_DRAW_BP` is
set, and a diagnostic that moved with an A/B arm would be reporting the arm rather than the game.
