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

## The target design: breakpoints PARTITION the turn (USER, 2026-08-20/21)

> "Essentially, we should only be considering spells that have not been considered already at every
> point, making the breakpoints fully distinct from each other." — "And the only way to do this is
> through a mix of condemnation and only planning your section of the turn."

The invariant: main 1 plans **up to the first draw**; that draw's continuation plans up to the next
draw; each decision point considers only spells no earlier point has considered. Breakpoints then
carve the turn into disjoint sections, and there are no permutation duplicates to burn nodes on.

Two mechanisms, and **neither works alone**:

* **Condemnation** enforces "not already considered". `MTG_BP_CLASSIFY` + the pre-draw hand snapshot
  (`g_bp_hand_before`, bound at the arming site) drop a card the plan already declined, while keeping
  anything genuinely new — a drawn card is a duplicate of nothing.
* **Plan truncation** enforces "your section only". The base plan must stop at the first drawing
  cast instead of committing the whole turn past information it does not have yet.

### What the shipped class gets wrong, measured

Site 6 as committed is **deferred** (the re-solve runs after every main cast), so the base plan plans
straight through two or three Puresteel draws. That is the opposite of truncation, and the numbers
show it. Same 8 games, seed 900001, d3:

| | continuations / breakpoint | max | rank-unreachable | site-6 reaches |
|---|---|---|---|---|
| condemnation OFF (as committed) | 9.47 | 146 | 80.1% | 1,049,685 |
| condemnation ON | **4.56** | **38** | 59.5% | **734,707** (-30%) |

Condemnation halves the breadth and cuts the worst case 4x — but 4.56 is nowhere near the ~1-2 the
invariant implies, because the continuation is still facing a whole hand rather than owning a
section. It also explains the realisation gap reported above (the drawn card is spent in the phase
that drew it just 1 time in 29): with the whole turn already committed and the trailing equip passes
already run, there is usually nothing left to spend.

**Implementation consequence**: site 6 has to move from deferred to INLINE AT THE FIRST DRAW, with
the base plan truncated there. The constraint that originally pushed it to deferred is real and must
be handled rather than rediscovered — the rollout's deferred re-solve runs *after* the trailing
Equip/Stoneforge/Balan passes and so does the executor's committed replay, whereas an inline
continuation runs *before* them, and equip costs move with metalcraft. Sites 0 and 1 are already
inline and carry an executor hook (`is_draw_engine`), so there is a pattern to copy.

### OPEN HAZARD: a drawn ENABLER revalues condemned cards (USER, 2026-08-21)

> "if you drew a puresteel paladin mid-turn there might be some advantage to considering equipment
> that has been condemned ... There are decks that have aspects of this idea, though, that may
> require some extra thinking."

Condemnation assumes a declined card **stays** declined — that more information can only confirm the
pass. Drawing an *enabler* breaks that monotonicity: it retroactively raises the value of cards
already passed on. The sharp case here is drawing a SECOND Puresteel off an equipment ETB, after
which every equipment condemned earlier that turn is worth more, because each later ETB now draws
two.

Why it is largely inert on THIS deck, and the mechanical reason as well as the structural one:
* the deck's only mid-main draw source IS an equipment ETB, so there is no independent draw step that
  could hand us an enabler out of nowhere; and
* **cast order defuses the rest**: Puresteel ranks ahead of Equipment (6 vs 8 reviewed, creature 10
  vs noncreature 20 generic), so a drawn Paladin is cast FIRST and the equipment following it is a
  continuation decision rather than a condemned one. The same ordering argument answers the tutor
  variant the user raises ("we would just list that spell first").

### The sharper case, and why a RANGE turns out not to be needed (USER, 2026-08-21)

> "cards that deal damage based on instant/sorceries cast and cantrips. The cantrips can be used to
> cast the spell or to help it go off, which means you may want to cast one cantrip before and one
> after. I guess that just means we need a range." — then: "maybe that is fine as-is, because you
> find guttersnipe and then prioritize casting it before other spells ... The cantrips can both find
> and enable a card like Guttersnipe."

The second reading is the right one, and three things already in the tree carry it:

* **`BpPlanCasts` never condemns a name the plan CASTS.** So "one cantrip before, one after" already
  survives for the same cantrip: casting one Ponder keeps Ponder considerable at every later
  breakpoint. The residual gap is only ACROSS names (cast Ponder, decline Preordain, then want
  Preordain after the payoff).
* **Ordering dissolves most of that residue.** The payoff ranks ahead of the cantrips, and the
  cantrips are themselves the breakpoints, so "before and after" falls out of the SEQUENCE OF
  SECTIONS rather than needing a range inside one section. Dig with cantrips until the payoff is
  found; the payoff is new (and so first-class) at the breakpoint that drew it; cantrips cast after
  it are later sections.
* **The card class does not exist yet, and the hook is already sited.** `MayPrecedeCantrip` names
  Guttersnipe explicitly and records that the payoff polarity is "currently EMPTY, not mis-modelled"
  — the pool's only cast-trigger cards are Eidolon (punisher polarity, damages the caster), Worthy
  Knight, Aria of Flame and Mana Cannons. When a payoff-polarity card is implemented it needs a
  param, and that check goes there.

### IMPLEMENTATION TRAP: truncation redefines "declined"

Today a card in hand the plan does not cast is treated as declined, and condemned. That is only
sound because the plan spans the WHOLE turn. Once the plan stops at the first draw, most of the hand
is not declined — it was never reached. Condemning it would be exactly the over-prune this design is
trying to avoid.

So the two mechanisms must be co-designed: the condemned set has to become **"cards this section
could have cast BEFORE the draw and passed over"**, not "cards this section did not cast". Shipping
truncation on top of today's condemnation rule without that change would silently delete most of the
turn.

**The general fix, for the deck that does need it: condemnation INVALIDATION.** When a continuation
casts something that changes another card's value, un-condemn the class it enables. That needs a
representation of "X enables Y" that is param-derived rather than a name list — and note that this is
the same blind spot recorded at the top of this document, since the enabling is owned by the
PERMANENT, not by the card being re-considered. Deliberately unbuilt: no current deck exercises it,
and building it against a hypothetical would fix the wrong shape.
