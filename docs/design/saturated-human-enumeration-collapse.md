# Saturated-mana enumeration collapse (human play only)

**Status: implemented 2026-09-10, all gates green (scenarios 73/73, smoke ALL PASS byte-identical,
307-reference sweep 0 play-drift / 0 enum-gap / 0 contract-fail).** Flag `MTG_HUMAN_SAT_ENUM`,
**default ON**, `=0` restores the previous behaviour exactly.

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
4. **No candidate ADDS mana** — no `ritual_float`, no `rock_mana`, no `etb_untap_lands` (Peregrine
   Drake / Cloud of Faeries). With 3 and 4 established, mana is monotonically *consumed*, so an
   action the pool cannot pay alone cannot become payable by adding more actions.
5. The pool — floating **plus every untapped source**, i.e. the enumerator's own `pool` — can pay,
   **as one lump and by the engine's own payer predicate (`ManaPool::CanPayFlat`)**,
   `MTG_HUMAN_SAT_FACTOR` (**1**) × the **maximal demand any single enumerated plan can carry**: the
   dearest *individually-payable* member of each mutually-exclusive group plus every
   individually-payable independent action.

Anything the lump check cannot honestly price — an `{X}` cost, a snow pip — answers **false** and the
full fan runs. *When the predicate is not certain, fall back to full enumeration.*

Factor **1** is the exact non-interaction condition, not a shortcut: if the pool can pay every
candidate *simultaneously*, no subset and no order can make another action unaffordable. (Factor 2
was the first cut and it did **not** fire on the very frame this exists for — the three uncastable
Eldrazi Displacers in hand doubled a white demand the board pays out of four wild units. Restricting
the demand to individually-payable candidates, which clause 4 makes sound, is what fixed it.)

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
* `MTG_HUMAN_SAT_MAXACT` (**4**): combinations survive up to 4 chosen actions. The collapse is sound
  at 1 (a 5-action plan is a 4-action commit plus one more click); the cap is set by *what a saved
  reference actually picked* at or above the floating floor — EDF s8_gi7 T3's *"Conservatory:
  investigate, Mariposa Military Base: draw a card, Eldrazi Displacer: blink, Clue Token:
  sacrifice"*. The fan's action-count histogram is binomial, so 4 already keeps only 8,378 of
  101,568 plans while costing no user-owned line its recorded plan.

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

## Gates

* `bash test/scenarios.sh` — 73 passed, 0 failed.
* `bash test/regression.sh --smoke` — 73 passed, 0 failed, **every digest byte-identical**; and
  again with `MTG_HUMAN_SAT_ENUM=0`, also ALL PASS.
* `python3 test/viewer_protocol_check.py --threads 16 --strict` — 307 refs, **0 play-drift, 0
  enum-gap, 0 mull-drift, 0 contract-fail** (12 ok / 295 repaired, versus 13 ok / 294 repaired
  before: one reference now needs an index repair instead of an exact index, which is what a shorter
  plan list means and is not a regression).
* `python3 test/interactive_parity_check.py` — both games byte-identical frame for frame.
