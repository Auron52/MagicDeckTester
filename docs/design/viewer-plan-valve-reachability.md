# The viewer plan valve: an honest size, and a reachability guarantee

> "⚠ This board's plan space is too large to enumerate in full (476427292645799936 combinations) …
> Every action is still reachable one click at a time."
> — the viewer, to the USER, repeatedly, on KittyEquipment v2

> "That's actually an incorrect message. I can't equip various equipment."
> — USER, 2026-10-02

Both halves of that exchange were real defects, and they were different defects. The number was
wrong, and the promise was false. This documents what each one was, what fixed it, and the two
things the investigation refuted on the way — including one of my own conclusions from earlier the
same day.

Status: **shipped**, human play only. Autonomous play, rollouts and ground truth are untouched by
construction (every change lives inside `CapGroupsBySituationalRank`'s `valve` branch, which
requires `HumanPlayActive()`), and that is measured rather than argued: smoke **107 passed, 0 failed,
0 configs changed, 0 play-changed**.

## 1. The number was computed by a rule the engine had stopped following

`viewerplancap::Estimate` (EngineFlags.h) prices a frame as

```
raw = 2^independents x PROD over groups (1 + |group|)
```

a product over **digits**. That was right when it was written. It stopped being right when
`MTG_EQUIP_COPY_COLLAPSE` went **default-ON (2026-10-01)**, because that collapse does not remove
digits — it restricts the walk to positions whose interchangeable-copy digits are *non-increasing in
group order*, and `MTG_EQUIP_COPY_SKIP` jumps the odometer past the rest. The positions actually
visited are the **canonical** ones; the estimate kept multiplying as though every copy were free to
vary independently.

For a class of `n` interchangeable groups each of width `w`, the unfolded product contributes
`(1+w)^n` but the walk visits only the non-increasing sequences, of which there are `C(n+w, n)`.
Measured on the board that produced the report (`MTG_VIEWER_VALVE_DIAG`):

```
[valve] groups=44 ind=0 equip_groups=32 classes=5 fold=0.0002178
        raw_pay=7.58997e+18   folded_pay=1.65299e+15   pcap=4096
```

**`fold = 0.0002178` — the figure shown to the player was 4,591x too large.** `FungibleEquipFoldRatio`
now applies that factor, so the valve both reports and *decides on* a number that matches the walk.

**Why the valve's payable-knapsack refinement did not save it.** `Estimate`'s second component counts
only selections whose summed **mana value** fits `ManaPruneBound` — added so a tutor fan whose members
mostly cannot be paid stops inflating the product (the gi4 Green Sun's Zenith incident). On a
Puresteel Paladin board metalcraft makes **every** equip `{0}`, and the deck's five shield classes are
all `{0}` to cast, so the knapsack constrains nothing and `pay` degenerates to exactly `raw`. The
refinement that exists to stop over-counting is *inert on precisely the deck that over-counts worst*.

## 2. The promise was false, and the cause was the ranking, not the bound

`SituationalCardRank` ranks **cards**. On an equipment board the equips it ranks lowest are the
shields — `{0}` artifacts whose role on the list is the cast (metalcraft + a Sram/Puresteel draw),
not the attach. So a rank-ordered keep filled its whole budget with casts and offered **no equip at
all**, which is exactly the user's report. The history line then claimed every action was still
reachable one click at a time, which was not true of any of them.

Three changes, in the order the valve now applies them:

1. **Fold-aware estimate** (§1), so the shrink is decided on the real size.
2. **Pool interchangeable duplicates before dropping any action.** A class of `n` interchangeable
   copies offers the same menu entry `n` times; keeping one leaves every `(class -> host)` action
   clickable and costs only the ability to attach a *second* copy of that class **in the same line**,
   which the next line offers again. This is the one narrowing that keeps the promise.
3. **Seed one group per action KIND**, then rank-fill. This is the half that fixes the report: it
   guarantees a whole *class of play* cannot vanish, at the granularity a player reasons at. Within a
   kind the ranking still decides and combinations are still narrowed; nothing is un-bounded, because
   every candidate set is still tested against both caps.

Plus `break` -> `continue` in the rank-fill: the ranked order is by card rank, not by width, so the
group that overflows the bound is routinely followed by narrow ones that still fit, and `break` threw
those away for nothing.

**Measured, against HEAD built in a worktree** (`logs/viewerprobe/bighand.json` — the user's shape: a
wide metalcraft board plus a big duplicate hand, since Sram/Puresteel draw on every equipment cast):

| plan-space bound | base HEAD | with the fix |
|---|---|---|
| 65536 (default) | `choose` | `choose` |
| 16384 | `choose` | `choose` |
| 4096 | `legal_not_enumerated` | **`choose`** |
| 1024 | `legal_not_enumerated` | **`choose`** |
| 256 | `legal_not_enumerated` | **`choose`** |

The equip survives at bounds **256x tighter** than default, where base HEAD loses it.

### The first cut of the pooling measured INERT, and why

It gated the pooling on "does this alone bring the frame under the bound?". On the very board it was
written for, pooling 32 equip groups down to 5 classes still estimates ~1e6 against a 65,536 bound —
so the gate always failed, the pooling never applied, and the ranked drop ate the equips exactly as
before. Before/after was identical at every bound. Pooling is reachability-preserving whether or not
it is *sufficient*, and every group it removes is one the ranked drop no longer has to pay for, which
is what leaves room for the equips. It is now unconditional.

`MTG_VIEWER_VALVE_DIAG` exists because of this: the first fix was aimed at the wrong input (the
estimate) and there was no way to see that from outside.

## 3. Two refutations

**The valve is not always the mechanism, and raising the bound never fixes it.** With the valve fully
off (`MTG_VIEWER_PLAN_CAP=0`) a wide board still returned `legal_not_enumerated`. That matches an
earlier measurement (`2e7` still dropped 11 groups; `1e10` or valve-off took >7 min for ONE frame) and
closes the question: **do not retry raising the bound.** On that particular probe the suppression was
an entirely separate narrowing that only `MTG_UNPRUNED=1` opens — which the real viewer sets
session-wide, so it is not reachable from the viewer, but it is a trap for anyone reproducing this
with a `--scenario` fixture. A scenario is not a viewer; emulate one with `MTG_UNPRUNED=1
MTG_HUMAN_PLAY=1` or measure the wrong code path.

**The per-copy fungible fold was NOT missing — I claimed a ~23,000x win that was already banked.**
Reading the user's "Cathar's Shield and Accorder's Shield are duplicates … some of them should be
deduplicatable also, since they are all 4-ofs", I priced the interchangeable-copy fold against an
unfolded `3^20` baseline and reported a 23,000x opportunity. `MTG_EQUIP_COPY_COLLAPSE` had been
default-ON since the previous day, so that baseline had not existed for 24 hours. The real incremental
win from the cross-name observation is ~5x on the equip axis (see §4), and the actionable finding was
the *estimate*, not the fold. (Memory: `verify-done-claims-in-tree` — check flag defaults and adoption
commits before pricing anything as missing.)

## 4. Cathar's Shield == Accorder's Shield: `behaviour_identity`

The user's observation is correct and verified in the card data, not assumed:

```
Cathar's Shield    {0}  {equip_cost_generic:3, equip_grants_vigilance:true, equip_tough_bonus:3, is_equipment:true}
Accorder's Shield  {0}  {equip_cost_generic:3, equip_grants_vigilance:true, equip_tough_bonus:3, is_equipment:true}
```

Byte-identical parameters, same cost, keywords, types and subtypes. They differ in the name string
and in oracle prose (which carries the `[bracket note]` modelling commentary). But
`BuildFungibleEquipClasses`'s signature opens with `"E|" + a0.card_name`, so the two land in different
classes and their cross-name symmetry is never folded: v2's equip classes are five of size 4 instead
of `{4, 8, 4, 4}`. On a 2-host board that is `15x15 -> 45`, a **5x** cut on the equip axis.

`CardDefinition::behaviour_identity` is a digest of **the whole cards.json entry minus `name` and
`oracle_text`**, and the direction of that choice is load-bearing: including an irrelevant key only
makes two cards look *different*, which declines a fold, whereas omitting a relevant one makes them
look the *same* and licenses a wrong collapse. So it fails toward "not interchangeable", and a param
invented tomorrow is covered with no edit. This is the discipline `LoadFromJson`'s subtype
pre-interning already adopted ("read straight off the RAW JSON, mechanical and complete, which naming
the param fields one at a time was not") after a hand-written list covered 4 of ~15 params. It is also
exactly the hazard `BuildFungibleEquipClasses`'s own counter list names: *"this list is 'every field
that can differentiate two copies' and an incomplete one is the documented failure mode"*.

**Audited across all 486 cards**, the collisions are:

| cards | equipment? |
|---|---|
| Cathar's Shield, Accorder's Shield | **yes** |
| Muscle Sliver, Predatory Sliver | no |
| Cavern of Souls, Unclaimed Territory | no |
| Dauntless Bodyguard, Venerable Knight | no |
| Remand, Memory Lapse | no |
| Rancor, Audacity | no |
| Soul Warden, Soul's Attendant | no |
| Elvish Mystic, Llanowar Elves, Fyndhorn Elves | no |

So the fold's reach is exactly the pair the user identified and nothing more — the other seven are
non-equipment and `BuildFungibleEquipClasses` is gated on `is_equipment`.

**Behind `MTG_EQUIP_COPY_XNAME`, default OFF, and NOT adopted.** It is a sound identity fold, but its
parent collapse is already documented as able to move play digests (the surviving representative can be
a different physical copy on an isomorphic board), and on the probe board it measured **inert** —
identical plan indices with it on and off. It is built, tested and recorded; it is not claimed as a win.

**Incidental finding, for the user's judgement, not acted on:** `Remand`/`Memory Lapse` and
`Rancor`/`Audacity` are *not* the same card in real Magic (Remand returns the spell to hand and draws;
Memory Lapse puts it on top of the library — and Rancor returns itself from the graveyard). Their
entries being byte-identical is a modelling simplification the audit surfaced by accident. Card rulings
are user-owned, so this is reported rather than changed.

## 5. What the player now sees

The history line distinguishes the two narrowings, because they are different promises:

* **dropped** — "N group(s) of choices were DROPPED, so some actions are not offered this turn." No
  false reachability claim.
* **pooled** — "N interchangeable duplicate(s) were pooled: identical Equipment is offered once instead
  of once per copy. Every action is still offered; attach a further copy on the next line."

`plans_truncated` carries `pooled_groups` alongside `dropped_groups` for the same reason, and
`test/viewer_plan_space_check.py`'s assertion 4 now accepts either (a dropped-groups-only test reads a
pooling-only frame as an *unreported* truncation, which is backwards — that is the frame that cost the
player nothing). That gap was unreachable on the EDF walk the gate drives, because that deck has no
Equipment and nothing there can pool.

## 5b. "Equip all free" — why it only equipped a few, and what is left

> "Equip all free was available, but it only equipped a few."
> — USER, 2026-10-02

Three causes, in the order they were found. **None of them is the valve**, which is worth stating
plainly because two of my own diagnoses in this document pointed there: the groups the valve drops
on these frames are CASTS (measured `dropped_names`: Golem-Skin Gauntlets, Dwalin, Colossus Hammer).

1. **`MTG_PLAY_PLANS_CAP` is 200.** The gesture scanned `d.plans`, which is a ranked 200-plan slice
   of tens of thousands. On a T4 frame with 14 loose pieces, **zero** had an equip action anywhere
   in the emitted slice. Scanning a ranked window for one KIND of action is the wrong instrument.
2. **The affordance must name the copy the ENGINE offers.** Deriving pieces from board state made
   every entry unenumerable:
   ```
   equip=Accorder's Shield       -> choose
   equip=Accorder's Shield@40    -> accept
   equip=Accorder's Shield#3@40  -> legal_not_enumerated
   ```
   `MTG_EQUIP_COPY_COLLAPSE` keeps one canonical representative per interchangeable class, and its
   own note warns "the surviving representative can put a different PHYSICAL copy on the host".
   `free_equips` is therefore harvested from the enumerated actions' `sac_source_id`. An affordance
   that offers unenumerable moves is worse than none.
3. **On those frames the engine enumerates no free equip for an unattached piece at all**, so the
   honest affordance correctly offers nothing. A bundle fails at k=1 as surely as at k=10.

### 5c. The fix was the user's own proposal, and it is now BUILT

> "Maybe equip all free to x." / "We shouldn't be counting on the unpruned search to fit."
> — USER, 2026-10-02

**`Action::Kind::AttachAllFreeEquipment`**: one enumerated action per legal host, every host in
**one** mutual-exclusion family, instead of N independent equip digits the bound can never all hold.
14 pieces × ~4 hosts is `4^14` against 65,536, so at most ~8 could EVER be separate digits whatever
the keep policy; one group of `(1 + hosts)` replaces that product outright, and because every member
costs `{0}` the **free-group seed** (§2) keeps it.

Measured on the staged wide board — 20 loose pieces in five four-of classes, two legal hosts:

```
equipallfree=0                 -> choose, 2 variants
   variant 596  equip all free (20) -> Puresteel Paladin
   variant 597  equip all free (20) -> Sram, Senior Edificer
equipallfree=0;equipallfree=0   -> legal_not_enumerated   (one family: never two bundles in a plan)
equipallfree=999999             -> legal_not_enumerated   (no such host)
```

Balan's **apply could not be reused**, and the reason is a rules question, not a convenience one:
`ApplyAttachAllEquipment` attaches *every* Equipment and **bypasses equip costs** — correct for a
printed ability (that is Balan's whole point against Colossus Hammer's `{8}`) and a rules violation
on an arbitrary creature. The bundle is shorthand for N Equip *activations*, so it re-prices every
piece through `EquipCostGenericNow` **at apply, per piece**, and skips any that is no longer free.
Its own selection, in `FreeAttachableEquipment`: unattached, `{0}` now, not animated, with
`equip_sacrifices_prior_host` and `equip_grants_shroud` carved out, and min-power pieces lifted by
the bundle's own banked power and returned last so the attaches happen in that order.

**ONE PREDICATE, FOUR READERS** — the enumeration's offer gate, the menu label's count, the published
`free_equip_all` affordance, and the apply. A menu entry promising an attach the apply then declines
is the defect class `CanAttachEquip` exists to close, and a bundle multiplies it by N; one of the
nine unit tests in `test/unit/test_equip_all_free.cpp` asserts the predicate's size equals the
apply's return value over a board holding one of every shape.

**Emitted only under `HumanPlayActive()`.** That is the correctness argument for adding an action to
the enumerator at all: the search never sees the kind, so every autonomous decision and every GT
number is byte-identical *by construction*. It is also the right division of labour — the AUTO-EQUIP
collapse already force-includes the best mass-equip line for the search, and that collapse is itself
gated `&& !HumanPlayActive()`, so the two are exact complements.

Three bugs found while building it, each worth recording because each is a repeat of a lesson this
file already holds:

* **The pass shortcut at CheckLine stage 0 had to learn the new verb.** Its own comment says so —
  *"EVERY new verb must be added here or a line made up ONLY of it silently grades `accept /
  plan_index -1 / pass`"* — and the first probe did exactly that: `equipallfree=999999` graded
  **accept**. An accept for a line the engine then does not play is strictly worse than a reject,
  because the player sees no error at all.
* **The host needed its own sub-decision token.** One action per host, all carrying the host's name
  as `card_name`, so without a sub they share a dedup signature: a wildcard `equipallfree=0` on a
  two-host board graded `accept / 1 variant` and only one host was reachable. Third time this exact
  collapse has been found in `TurnSolver.cpp` — loyalty, then `Equip`, now the bundle.
* **The piece count must NOT ride `Action::chosen_x`.** `chosen_x > 0` is the *catch-all* arm of the
  sub-decision builder, so carrying the count there produced a literal `X=20` variant token — a
  "choose how to resolve" dialog asking the player about an internal number, which is precisely the
  dialog spam the blink/Jitte carve-out beside it exists to suppress. The count is recomputed from
  the shared predicate instead.

### Two measurement traps, both the wrong predicate

* **Counting equips without filtering to UNATTACHED counts MOVES.** This produced a reported "free
  equips reachable 0 -> 7" when the real figure was 0-1, which went into a commit message before it
  was caught. Retracted in `b1d835c3`.
* **A bare `equip=<name>@<host>` matches ANY copy**, so an `accept` does not prove a LOOSE piece is
  enumerable — it was matching a move of an already-attached copy. That ambiguity is exactly why the
  `#<src>` form exists.

### Two more reporting bugs in the valve's own numbers, both user-spotted

* `kept_positions` reported **3265173504 against a 65,536 bound** — impossible. The pooling step
  wrote the field unconditionally, and because it is a `std::max` across the per-land inner calls an
  INTERMEDIATE post-pooling estimate latched and beat the real final figure.
* `positions_full` printed **-9223372036854775808**: the raw product is a double that runs past
  1e19, so `static_cast<long long>` was UB. `viewerplancap::PositionsText` now prints integers below
  1e15 exactly and 3 significant figures above.

Both are the same species as §1 — a number shown to the player that no stage of the engine ever
produced — which is the argument for making the history line say something checkable: the user found
all three of these by reading it.

## 6. Guards

* `test/scenarios/kittyv2_viewer_wide_equip_board_still_offers_equip.json` — a staged wide metalcraft
  board; `validate_line: "equip=Kite Shield"` must not come back `legal_not_enumerated`. It pins the
  user-visible consequence rather than the internals, so it fails if a future change goes back to
  deleting equip groups.
* `test/scenarios/kittyv2_equip_all_free_is_one_action.json` — the same board, asserting
  `equipallfree=0` grades `choose` with **2** variants both labelled `equip all free (20)`. The
  variant COUNT is the assertion that both hosts survive the sub-decision dedup; the label
  substring is the assertion that the bundle really covers all twenty pieces and not a handful.
* `test/unit/test_equip_all_free.cpp` — nine cases over `FreeAttachableEquipment` and
  `ApplyAttachAllFreeEquipment`: the happy path, the no-metalcraft control (nothing in this deck
  has a printed equip of `{0}`, so with the grant gone the bundle is empty — the two tests are each
  other's control), each of the four carve-outs, the min-power lift and its negative, and the
  predicate-equals-apply invariant. **These are the primary pin**, because the action is human-play
  only: no seed-driven regression run executes this code at all, so the suite cannot test it.
* `test/viewer_client_check.js` — both routes: the N-token selection logic (see
  `viewer-line-macros.md` §Feature 4) and the single-action route, which asserts `free_equip_all` is
  PREFERRED over `free_equips` (the stub deliberately makes the two disagree, 11 pieces vs 2), that
  exactly ONE plan entry is queued, and that it encodes to `equipallfree=<host>`.
* `test/viewer_linebuild_check.js` — `equipallfree=<host m_number>` encoding, including the
  no-`hostNum` form falling back to the engine's `0` wildcard rather than a malformed token.
* `test/viewer_checks.sh` — **FAILS, and BOTH of its failures are PRE-EXISTING.** Verified by running
  the identical checks against HEAD built in a worktree:
  * *protocol check*, `--strict`: tally reproduces exactly — `35 ok, 306 repaired, 0 play-drift,
    0 shuffle-dead, 3 board-diverged, 1 enum-gap (Hinata2), 10 mull-drift (CritterLifegain),
    0 contract-fail` over 355 refs. The number that matters for a change that alters a human-play
    MENU is **0 play-drift**: no saved reference's recorded plan index moved. (A menu change shifting
    indices is the real hazard here — references replay recorded click indices — so this is the
    assertion that licenses the valve work, not the aggregate pass/fail.)
  * *validate-line check*: `1646 accept, 229 choose, 0 unsupported, 317 skipped, 9 known-fail(v1
    limits), **9 REGRESSION**` — and the base worktree prints the same line and the same nine
    entries, diffed byte-for-byte. They are Snow `NO_VALIDATION_BLOCK` / blue-source payment lines
    plus one Auras `{W}{W}` line; Snow additionally carries an uncommitted GT rebaseline
    (memory: `snow-cost-levers-2026-09-30`), which is the likely cause. **Not investigated here, and
    not caused here.** Do not read this gate as green, and do not read it as a regression from this
    branch; equally, do not rebaseline it on the strength of this work.
* `test/viewer_plan_space_check.py` — **PRE-EXISTING FAILURE, not caused by this work and not fixed by
  it:** the EDF HANG-1 line does not reach a terminal within 400 decisions. Base HEAD built in a
  worktree fails the identical assertion with the identical biggest-plan-list (57,343); this branch is
  slightly faster on it (worst frame 0.58s vs 0.66s, walk 15.7s vs 19.4s CPU). The frame-count and
  per-frame CPU assertions pass on both. Do not read this gate as green, and do not read it as a
  regression from here.

## The player's override: `--full-enum` (2026-10-03)

**USER:** *"Getting more unnecessary rejections … If possible I would like to change things in a way
that allows these lines to be played, but to record that the search failed rather than preventing me
from proceeding. It is quite the nuisance otherwise."*

### Why no choice of drop victim can fix this

The valve must drop a group on any frame over its bound, and **whatever it drops makes some
rules-legal line unreachable.** Measured on the reported frame
(`logs/play/rejections/KittyEquipment_cod_v2-puresteel-hammer_s15_gi14_t5.json`, seed 15 / gi 14 /
turn 5), with `MTG_VIEWER_VALVE_DIAG=1`:

```
groups=12 ind=0 equip_groups=6 classes=0 fold=1 raw_pay=393216 folded_pay=393216 pcap=65536
KEEP  Golem-Skin Gauntlets  kind=0 cast=1 width=1 min_mv=1 afford=1
DROP  Golem-Skin Gauntlets  kind=0 cast=1 width=1 min_mv=1 afford=1
```

393,216 payable positions against a 65,536 bound, nothing pooled (`classes=0 fold=1`), and the group
it dropped was the **second hand copy** of Golem-Skin Gauntlets — so "cast two Gauntlets this turn"
was unreachable while every distinct card stayed castable. That is not a bad choice of victim: the
cover rule (`78ff03aa`) deliberately guarantees one group per distinct **card**, and a second copy of
one card is outside that guarantee by construction. Raising the bound only moves which line is
unreachable, which is why it has now been refuted three times on this deck.

Bisected to be sure, rather than assumed — the previous two diagnoses of this frame were both wrong:

| line | verdict |
|---|---|
| the user's full line | `legal_not_enumerated` |
| same, one Gauntlets | **`accept`** (idx 88434) |
| both Gauntlets only | `legal_not_enumerated` |
| `MTG_FREE_CAST_HOIST=0` / `EQUIP_COPY_SKIP=0` / `EQUIP_COPY_COLLAPSE=0` / `FOLD_SEARCH_ODO=0` | all still `legal_not_enumerated` |
| `MTG_VIEWER_PLAN_CAP=0` | **`accept`** |

So none of the adopted search collapses is responsible, and `MTG_COST_REFRAME=1` does not help
either despite the verdict naming a same-turn cost reducer.

### The design

`viewerplancap::SuspendScope` + a `--full-enum <ordinals>` side channel. Listed main ordinals are
enumerated with the valve suspended, so the line is there to be indexed. Verified on the frame:
`accept`, plan index 135.

**Three decisions worth keeping:**

1. **A SIDE CHANNEL, not a stdin directive.** An in-place `@full-enum` was built and then removed.
   The index is positional, so the parked frame's menu, the next `/api/step`'s menu and the saved
   reference's menu must all agree; a directive would have let them diverge. Putting the ordinal in
   the side channel changes the server's session key, so the existing respawn machinery re-runs the
   prefix with the flag set and every process sees one menu. It costs a prefix replay — a slow click
   instead of a refusal, which is the trade the user asked for.
2. **It must be RECORDED, and that is not cosmetic.** The same index resolves to a different line:
   replaying `…,135` with `--full-enum 10` reaches a 13-permanent board, without it a
   12-permanent one. So the trace writes `"full_enum": true` on the frame, and
   `viewer_protocol_check.py`, `viewer_validate_check.js` and `logs/replay_ref.py` all reconstruct
   `--full-enum` from it. Omitting that would have manufactured the play-drift class on purpose.
3. **Offered ONLY for `legal_not_enumerated`.** That verdict is the engine's own statement that it
   simulated the line and it is rules-legal; only the enumerator never produced it. `illegal` and
   `unsupported` are the engine saying the line cannot be played, and the override must never
   reach them.

The engine emits a `search_gap` play event when it fires, carrying both menu sizes, and the viewer
writes a history line before retrying — so a played-anyway line is on the record as an enumeration
gap rather than as a clean play. That is the half of the request that is not "let me proceed".

Inert when unused: smoke 107 passed / 0 failed, `configs changed: 0`, `play-changed=0`.
