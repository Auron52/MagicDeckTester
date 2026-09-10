# Saturated-mana enumeration collapse (human play only)

**Status: implemented 2026-09-10; extended the same day to cover MANA-ADDERS (the first cut never
armed on this deck's real go-off), K raised 4 → 6 on measured evidence, and a payload-aware display
cap added alongside. All gates green — see the Gates section.** Flag `MTG_HUMAN_SAT_ENUM`,
**default ON**, `=0` restores the previous behaviour exactly.

Levers, all documented at their read site in `src/ai/TurnSolver.cpp`:

| flag | default | what it does |
|---|---|---|
| `MTG_HUMAN_SAT_ENUM` | on | master; `=0` restores the full fan |
| `MTG_HUMAN_SAT_ORDER` | on | the cast-order half |
| `MTG_HUMAN_SAT_SUBSET` | on | the subset half |
| `MTG_HUMAN_SAT_FLOOR` | 24 | minimum floating mana before anything collapses |
| `MTG_HUMAN_SAT_FACTOR` | 1 | safety factor on the lump demand |
| `MTG_HUMAN_SAT_MAXACT` | 6 | largest number of chosen actions a collapsed plan may carry |
| `MTG_HUMAN_SAT_MINFAN` | 256 | projected odometer positions before the subset half engages |
| `MTG_HUMAN_SAT_DIAG` | off | per-evaluation predicate trace on stderr |
| `MTG_HUMAN_SAT_LEGACY_ADDER_BAIL` | off | A/B control: the first cut's mana-adder bail |
| `MTG_PLAY_CAP_PAYLOAD_COVER` (`src/main.cpp`) | on | A/B control for the payload-aware display cap |

The user report it answers (2026-09-09/10, EDF in the play viewer): *"Darn it just died with
ETIMEDOUT. I will need the fix for sure."*

## The defect

`tools/play/server.js`'s persistent `--interactive` child (`viewer-interactive-prefix-cache.md`,
commit 0f8ce63a) removed the prefix **replay** cost, so each click now pays only its own frame. But
that frame's cost *grows without bound* while a human hand-drives EldraziDisplacerFlicker's blink
loop (Emiel / Eldrazi Displacer flickering Peregrine Drake or Cloud of Faeries, banking floating
mana): the main-phase plan fan went **1,400 → 208,392 plans** across one session, a click finally
exceeded `STEP_TIMEOUT_MS = 120000`, and the ETIMEDOUT killed the game — the stateless fallback
timing out too.

Measured on the reproducible seed-8 game-index-7 turn-3 go-off frame (`--claude-play` replay of
`references/EldraziDisplacerFlicker/claude_s8_gi7.json`), **101,568 plans, 4.7 s of CPU for ONE
click**, the fan decomposes as:

| what | count |
|---|---|
| emitted plans | 101,568 |
| distinct (land, action **multiset**) | 12,672 |
| distinct (land, action **set**) | 9,216 |
| distinct **(action, target) payloads** | **23** |

So **8.0x of the fan is pure cast-ORDER permutation** and the rest is the odometer's subset
cross-product over 23 real choices. The ordering half is also where the time is: the expansion
`ApplyPlanDirect`s **every candidate ordering on a `GameState` copy**, up to `5! = 120` per
surviving action set. That is the `ApplyPlanDirect`-inside-enumeration the gdb burst samples landed
in, alongside `TapForCostSharedOnce` and `BuildSimKey`.

## The predicate — what "saturated" means

`HumanEnumSaturated` (`src/ai/TurnSolver.cpp`, just above `EnumeratePlans`) answers *"can payment
choice matter here?"*. It is true only when **all** of:

1. `HumanPlayActive()` and `MTG_HUMAN_SAT_ENUM != 0`. False in every rollout (`HumanPlaySuppress`)
   and every autonomous run — the search is byte-identical by construction.
2. `state.floating_mana.Total() >= MTG_HUMAN_SAT_FLOOR` (**24**). The "deep in a go-off" gate; it is
   what keeps ordinary play, and every reference recorded there, on the full fan.
3. **No same-turn mana/cost credit is live** — no ritual float, mana rock, cost reducer,
   coloured-pip reducer, affinity, metalcraft or Hinata credit among the candidates. Those make
   affordability depend on *which other actions the subset takes*, which is exactly the interaction
   being assumed away.
4. The pool — floating **plus every untapped source**, i.e. the enumerator's own `pool` — can pay,
   **as one lump and by the engine's own payer predicate (`ManaPool::CanPayFlat`)**,
   `MTG_HUMAN_SAT_FACTOR` (**1**) × the **maximal demand any single enumerated plan can carry**: the
   dearest member of each mutually-exclusive group plus every independent action, **every candidate
   counted and no refund credited**.

Anything the lump check cannot honestly price — an `{X}` cost, a snow pip — answers **false** and the
full fan runs. *When the predicate is not certain, fall back to full enumeration.*

Factor **1** is the exact non-interaction condition, not a shortcut: if the pool can pay every
candidate *simultaneously*, no subset and no order can make another action unaffordable.

### The mana-adder clause, and why it had to go (2026-09-10)

The first cut carried a fifth clause: **bail outright if any candidate ADDS mana** (`ritual_float`,
`rock_mana`, `etb_untap_lands`). With no adder, mana is monotonically *consumed*, which in turn
justified pricing the lump over only the *individually-payable* candidates.

On this deck that clause is nearly always true, and it is true **exactly where the shortcut is
needed**: an EDF go-off almost always has a castable Peregrine Drake in hand or a blinkable Cloud of
Faeries. Measured on the user's own seed-9 gi=8 turn-4 session, **22 of 48** predicate evaluations
answered `MANA-ADDER in cands (Peregrine Drake) -> full fan`, and those were precisely the expensive
frames (float 62–90, a 4,704-plan fan, 230–812 ms per blink click). The shortcut never armed on the
go-off it exists for.

The bail is unnecessary once the lump is priced with **zero credit** over **every** candidate:

* if the pool alone covers every candidate's printed cost simultaneously, then at any point in any
  line supply is at least `pool − spent` and remaining cost at most `demand − spent`, colour for
  colour by `CanPayFlat`'s own accounting — so no ordering can fail to pay, and a refund only ever
  adds headroom on top. The refund is an engine-deterministic consequence of a resolution, not a plan
  dimension, so it cannot make two orderings differ in *what resolves*.
* it also settles the converse worry — a subset the flat pool rejects but `SubsetPayableSequential`
  admits *because* the untapper's own cast funds it (the untapper-reservation chain and its hoisted
  retry). Such a subset **needs** the refund; the lump test requires everything payable **without**
  it; so a board carrying one is by construction *not* saturated and gets the full fan. The two
  cannot both hold, which is exactly the separation wanted. Observed live: two frames on the user's
  line answer `saturated=0` with `pool(C0 wild0)` against a `{C}` demand — the `{C}` is reachable
  only through a mid-line untap, and the predicate correctly declines.

The price is that enumeration **optimism** now counts: a candidate the board cannot actually pay (the
seed-8 hand's three `{2}{W}` Eldrazi Displacers on a white-less board) still charges the lump its
pips. That is the conservative direction — it can only *refuse* to collapse — and it still arms on
both motivating frames. `MTG_HUMAN_SAT_LEGACY_ADDER_BAIL=1` restores the old rule; it is the
one-binary A/B control, and it is what makes a rejection artifact's raw pick indices replayable
against the engine they were recorded on.

## What collapses, and why each is outcome-preserving

**Cast ORDER** (`MTG_HUMAN_SAT_ORDER`). Two orderings of one cast set differ observably only when one
of them cannot *pay* for a later cast — that is what the ordering search exists to find and what
`Plan::would_drop` labels (*"Living Wish before Trace of Abundance spends the board's only white, so
Trace is dropped"*). Under saturation nothing is dropped in any order, so the survivors differ only
in the order permanents entered — zone permutation, the 2.26x `order_frag` the solve-key census
measured. `viewer_protocol_check.py`'s `find_plan` re-anchors a recorded pick by `(land, casts)`
precisely to *"tolerate a summary-format change or a dropped order variant"*, so a reference recorded
under the full fan still resolves.

**Redundant SUBSET combinations** (`MTG_HUMAN_SAT_SUBSET`). Human play re-prompts after **every**
committed line (`MTG_PLAY_SEGMENT_ALWAYS`, default on — *"Commit Line literally means let me play
more things"*), so a combined `{A,B}` plan is reachable by clicking A and then B, and under
saturation committing A cannot make B unaffordable. That is the whole argument, and it makes
`PlaySegmentAlwaysEnabled()` a **soundness precondition**: the two now share one reader in
`src/ai/EngineFlags.h` so the collapse cannot be left armed against a segment loop somebody turned
off.

**Kept distinct, by construction:** every activatable ability per target *and per count*
(`ActivateBlink` / `ActivatePermAbility` carry `chosen_x`), every dig/cycle action, every distinct
hand cast (including each tutor target and each aura target), the `combo_off` finisher entry, the
land-drop choice (and its fetch/face/scry sub-variants), and pass. All of those are single-action
plans, and the filter is a *size* test — it removes combinations, never identities.

## Two gates that make it safe rather than merely fast

* `MTG_HUMAN_SAT_MINFAN` (**256**): collapse subsets only once the projected odometer position count
  (`2^num_ind × Π(|g|+1)`, exact and O(groups)) exceeds this. Below it the full fan costs
  microseconds and there is nothing to buy — while collapsing it *would* drop combined plans a
  reference recorded on a perfectly cheap frame. Measured: without this gate an 11-plan EDF frame was
  cut to 4 and the sweep reported an **ENUM-GAP** for zero gain.
* `MTG_HUMAN_SAT_MAXACT` (**6**): combinations survive up to 6 chosen actions. The collapse is sound
  at 1 (a 7-action plan is a 6-action commit plus one more click), so this is a **menu-quality** knob,
  not a correctness one — and it was measured, not chosen.

  4 was the first cut, sized off the deepest combined plan any saved *reference* picked above the
  floating floor. **That set was not representative.** Replaying the user's own seed-9 gi=8 turn-4
  session frame-for-frame against the pre-collapse engine (content-anchored, `find_plan`'s own rule),
  K=4 silently re-anchored **seven** of their clicks onto a *shorter* plan and left the board **7
  floating mana light** by the end: the line they actually played uses 5- and 6-action plans (a blink
  plus three Clue cracks plus the two investigate lands). K=5 and K=6 both reproduce the line exactly
  (**0 drift, 0 anchor-fail over 47 matched frames**); 6 is taken for margin.

  The cost is only on the subset axis — 4,704 → 1,639 plans at K=6 versus 715 at K=4 — while the
  **order** collapse, the dominant term (8.0x on the frame that motivated all this), is untouched by
  K. A menu that drops the plan the player is clicking is the same class of defect as a display cap
  hiding their target, and it is not worth 50 ms.

## The display cap must be payload-aware too

USER 2026-09-10: *"sometimes the emiel or displacer can no longer choose targets."*

This is a **separate** defect from the collapse and it is fixed independently of it, in `src/main.cpp`
at the plan-emission site. The viewer's list is capped (`MTG_PLAY_PLANS_CAP`, 200) and the GUI builds
its target pickers from **that slice**, not from the engine's fan. The pre-existing diversity pass
keyed on the sorted **cast-name multiset**, which cannot tell *"Emiel: blink Cloud of Faeries"* from
*"Emiel: blink Peregrine Drake"* — one name, one key, **one slot**. On a fan that is mostly cast-order
permutation noise the rest of the cap then fills with rank-ordered permutations of whatever sorted
first, and a lower-ranked (outlet, target) pair falls out of the emitted slice entirely.

Nothing else notices: `viewer_protocol_check` runs **uncapped**, so the class is invisible there by
construction; the regression digests cover autonomous play; the frame is still well-formed JSON.

The fix is a **payload-coverage pass** that runs before the name-multiset pass (and after the
reserved `combo_off` slot): walk in rank order and take any plan introducing a
`(kind, card, source, target, count, mode, enchant target, tutor target)` payload not yet
represented. Same keying as the saturation collapse, applied to the *display* selection **even when
the saturation predicate itself is off**. The folded display axes stay folded (a tutor target
re-picked at resolution is not part of the identity, or the pass would re-import the very cross
product the tutor collapse removed), and it is capped-mode only, so the uncapped checker is
byte-identical.

**It is a real class, not a theoretical one.** `MTG_PLAY_CAP_PAYLOAD_COVER=0` is the A/B control, and
with it off all six configurations of `test/play_cap_coverage_check.py` fail — the user's own frames
lose `('Emiel the Blessed', 'Cloud of Faeries', 12)` on **22 of 50** frames. With it on: 228–238
frames across six configurations, every distinct payload present.

## Measured result

Per-click, **identical board states** (the reference's own repeated blink pick, banking +6 floating
mana per click at a constant 40,391-plan fan; wall clock on a contended box, so read the ratio):

| floating | plans OFF | ms OFF | plans ON | ms ON |
|---|---|---|---|---|
| 100 | 40,391 | 1198.8 | 1,490 | 23.5 |
| 106 | 40,391 | 1415.0 | 1,490 | 23.4 |
| 112 | 40,391 | 1421.6 | 1,490 | 23.0 |
| 118 | 40,391 | 1262.2 | 1,490 | 22.8 |

The worst frame in the corpus, measured as **child CPU** (load-robust, min of 3):

| | plans | per-click CPU |
|---|---|---|
| OFF | 101,568 | **4.72 s** |
| ON | 3,196 | **0.04 s** |

Whole-game stateless replay of the same reference line: **13.5 s → 2.84 s** user CPU.

Below the floating floor nothing changes — the float-22 frame in the table above is 40,391 plans and
~1.2 s on **both** arms, which is the floor doing its job.

### On the user's own seed-9 gi=8 turn-4 session (2026-09-10, after the adder fix)

Method: `logs/edf2/lockstep_bench.py` advances **two** `--interactive` children together. Arm A (the
engine the artifact's raw indices were recorded on, `MTG_HUMAN_SAT_LEGACY_ADDER_BAIL=1`) clicks the
artifact's index; arm B clicks whatever index carries the **same plan content**, resolved with
`viewer_protocol_check.find_plan`. So both walk the identical line and every number is a matched
state, not two lines that drifted apart. Arm B runs at the viewer's real cap of 200.

*(Two harness traps worth recording: `find_plan` returns a list **position**, which equals the engine
index only in an **uncapped** list — that is why the protocol checker always runs uncapped, and a
capped arm must read `plans[hit]["index"]`. And the artifact must be replayed with
`MTG_UNTAP_C_STARVED=0`: it predates a6cc7155, whose starved-`{C}` promotion legitimately moves the
line, after which the raw indices no longer reach the recorded frames at all.)*

| step | floating | arm A fan | arm A ms (cap 200) | arm B fan | arm B ms |
|---|---|---|---|---|---|
| 27 | 29 | 3,528 | 699 | 1,408 | 55 |
| 28 | 26 | 14,112 | **2,591** | 4,735 | **151** |
| 30–38 | 28–60 | 2,797 | 206–300 | 908 | 21–43 |
| 40–48 | 62–90 | 4,704 | 230–812 | 1,639 | **30–37** |

47 matched main-phase frames, **0 float drift, 0 anchor-fail** — arm B reproduces the user's line
exactly. The blink clicks that are the actual complaint go **230–812 ms → 30–37 ms**. One frame (the
14,112-plan one) remains at 151 ms; the box was at load ~35 from other jobs throughout, so treat the
absolute numbers as an upper bound and the ratios as the signal.

### Which half does what (measured, not assumed)

The coordinator's watch-item — *"under saturation payments spend float first, but confirm payments
genuinely tap nothing, else variants become observable"* — turned out to matter, and the lockstep is
what caught it. Isolating the halves on the user's line:

| arm B configuration | frames with float drift vs arm A |
|---|---|
| `MTG_HUMAN_SAT_SUBSET=0` (order collapse only) | **0** |
| `MTG_HUMAN_SAT_ORDER=0` (subset collapse only), K=4 | **7** |
| subset collapse at K=5 or K=6 | **0** |

So the **order** collapse is float-neutral in practice — its variants really do differ only in the
order permanents entered, which is what the soundness argument claims. The drift was entirely the
**subset** half's action cap being too tight, and raising K to 6 removes it. That is a much better
outcome than the alternative reading (that order variants are observably different), and it is only
visible because the two arms were compared on matched states rather than on aggregate timings.

## Gates

Re-run in full after the 2026-09-10 adder fix, the K=6 change and the payload-aware cap, on
a6cc7155 + this change:

* `bash test/scenarios.sh` — **73 passed, 0 failed, 0 error**.
* `bash test/regression.sh --smoke` — **73 passed, 0 failed, 0 new; ALL PASS**, every digest
  byte-identical; and again with `MTG_HUMAN_SAT_ENUM=0`, also ALL PASS (autonomous play is untouched
  by construction — the whole predicate is behind `HumanPlayActive()`).
* `python3 test/viewer_protocol_check.py --threads 12 --strict` — 307 refs, **0 play-drift,
  0 shuffle-dead, 0 enum-gap, 0 mull-drift, 0 contract-fail** (11 ok / 296 repaired, versus
  13 ok / 294 repaired before this work: two references now need an index repair instead of an exact
  index, which is what a changed emitted list means and is not a regression — `repaired` is
  informational, only drift and enum-gap gate).
* `python3 test/interactive_parity_check.py` — both games byte-identical frame for frame.
* `python3 test/human_line_order_check.py` — **PASS**, all 8 arms. No interaction with the
  order-as-is feature: `ReorderPlanCasts` sets `Plan::searched_order` itself, so the human's queued
  order is applied to whichever plan is committed regardless of whether the *enumerator* emitted
  order variants. (If anything the two fit together — with the human's order pinned, enumerating
  engine-side order variants for human play is redundant.)
* `python3 test/play_cap_coverage_check.py` — **PASS**, 228 frames over six configurations
  (default / `MTG_HUMAN_SAT_ENUM=0` / legacy-predicate × both `MTG_UNTAP_C_STARVED` settings),
  biggest fan 19,068. With `MTG_PLAY_CAP_PAYLOAD_COVER=0` it **fails all six**, which is what
  establishes the class.
