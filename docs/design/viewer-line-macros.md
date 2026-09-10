# Line macros — the play viewer's "do this N times" and "investigate & crack"

> "a shortcut for the create clue + sacrifice and maybe even a way to multistack draw + untap +
> draw + untap since it is really slow doing it manually, especially when the engine slows down."
> — USER, EldraziDisplacerFlicker sessions, 2026-09

Status: **shipped**, human play only. Two viewer gestures (always available, no flag) plus one new
engine behaviour they depend on, `MTG_UNTAP_LINE_DEMAND` (default **ON**, `=0` disables).

These are the convenience layer for the turns that are **not** a proven finish. A turn the engine can
prove is a kill is absorbed by the **COMBO OFF** button, which submits a verified plan index directly
and is untouched by any of this. What is left — building up, digging, partial loops — is where the
human clicks the same two activations twenty times, and that is what these two gestures are for.

## The design decision that shapes everything: expand in the VIEWER, not the engine

Both features are **pure queue edits**. They push ordinary entries into `S.plan` and change nothing
about the protocol, the line grammar, or what the engine is asked. The alternative — a compact
count token expanded engine-side — was rejected on three counts:

* **An engine `Plan` holds at most ONE activation of a given source.** "Blink ten times" is already
  not one plan; it is ten consecutive commits, which is exactly what `linebuild.js`'s
  `segmentParts` / `advanceTo` chain has done since the stacked-activation work. A count token would
  need a second, parallel mechanism for the same thing.
* **Reference replay stays free.** Each macro iteration commits as its own segment, encoding
  byte-for-byte what the human would have produced by clicking it out by hand. A saved reference
  therefore holds an ordinary sequence of ordinary lines, and `test/viewer_protocol_check.py`
  replays it with no new concept.
* **Byte-identity is by construction.** Nothing in the search, the rollouts or the autonomous
  executor can reach `tools/play/`.

## Feature 1 — fused "Investigate & crack"

One gesture: activate an Investigate source, then sacrifice the Clue it makes to draw.

Controlled by a **global, persisted option** (below), not asked per click.
The engine publishes `makes_clue: true` on any `ActivatePermAbility` whose mode is `TapInvestigate`
(`src/main.cpp`), and the viewer keys the affordance off **that flag, not a card name** — so a future
Investigate source is covered with no viewer change.

**It queues TWO entries, and the second is `defer`red.** This is forced, not a shortcut:

```
segment 1:  cast=Conservatory      the Investigate  ({4}, {T})
segment 2:  cast=Clue Token        the crack        ({2}, Sacrifice)
```

Enumeration reads the **battlefield**, and while segment 1 is being graded there is no Clue to
sacrifice. Written as one line it is `illegal` — *"'Clue Token' is not in hand"* — on any board, however
much mana is spare. That is pinned by `test/scenarios/edf_fused_clue_needs_two_lines.json`, which
exists so the deferral can be simplified away the day it stops being true.

The engine's own **clue-fusion doctrine** (fuse create+spend, payability-gated) is a *search*
shortcut and is explicitly excluded from human play — *"the viewer keeps per-action blinking and its
explicit FINISH plan"* (`DecisionProviders.cpp`, `EdfAutoGoOffAfterCasts`). This is the human-queue
analogue of the same idea, built with no engine change at all.

Removing **either** half removes both (`LB.removeFusedAt`): an Investigate whose Clue is never
cracked is a different play, not half of the one that was asked for. That includes clicking the
source *off* at its cap — the cap path in `toggleActivate` routes through the same helper, because a
bare splice left the deferred crack queued as an orphan segment that would sacrifice a Clue nothing
had made.

### The global option — and the regression that forced it

> "I think I would much prefer to have the clue creation as a global option. It's kind of a pain to
> constantly have pop-ups."
> — USER, on the first cut of this feature

**What the first cut got wrong.** Offering "Investigate & crack" as a *second entry beside* the plain
Investigate made `opts.length` **2** for a land whose only ability is Investigate — and
`toggleActivate` opens the picker on `opts.length > 1`. So a source that had always queued on one
click started opening a modal **every single time**, on the deck whose whole loop is investigating
over and over. A convenience feature put a dialog on the most-repeated click in the deck.

**The fix** is `clickActivationOptions` collapsing the pair back to **one** option, chosen by a
persistent pref. The important part is that this is *not* "ON = fused, OFF = ask":

| `clue_fuse` | one click on an Investigate source | dialog |
|---|---|---|
| **ON** (default) | queues the fused create+crack | none |
| OFF | queues the plain Investigate; crack the Clue whenever you like | none |

Neither state pops anything, because **the setting is the choice**. Leaving a picker on the OFF path
would have preserved the complaint for half the users, which is not what "global option" means.

Stored in `localStorage` under **`mdt_queue`**, deliberately a *separate* key from the existing
`mdt_surface` store. `mdt_surface` answers "show the modal, or auto-reply the AI's heuristic
default?" and every entry there names a `dec.type`; this answers "what does a board click queue?",
before any decision exists. The options menu shows it under its own **Queue shortcuts** heading for
the same reason — `mdt_surface`'s heading ("The engine still evaluates every decision either way") is
simply untrue of a queue preference.

Collapsed in `clickActivationOptions` rather than in the picker because that is the one chokepoint
the whole click path shares (`toggleActivate`, the `⟳` badge on `bfThumb`, `auraAttThumb`), so the
badge count and the picker can never disagree about how many activations a click chooses between.
A source that offers Investigate *plus* some other ability still opens the picker for that genuine
choice — with one investigate-flavoured entry in it, not two.

**Audited end to end, and the picker was the only recurring prompt.** Both halves of the fused line
were measured on a deliberately wide board (3 Clues, 8 untapped sources): `cast=Conservatory` and
`cast=Clue Token` each return `accept` with **exactly one variant**, so neither the "which copy" nor
the phantom-`X` dialog that the blink `*1` fix once had to kill can arise here. Nothing else in the
clue flow prompts.

*Honest limitation:* the second segment is graded on the board the first produces, so if the
Investigate spent the mana the crack needed, the crack comes back as an ordinary reject. That is the
truthful answer — the Clue is still on the battlefield and can be cracked next line — and it is the
same deal the human line order already makes: *"It's up to me to make sure the order is correct."*

## Feature 2 — repeat ×N

`⟲ Repeat ×N` on the plan bar stacks the whole queued block N times (2/3/4/5/8/10/15/20 via a central
dialog, the `.pbtns` idiom). The repetitions commit as consecutive segments without another click.

Offered only when the block is genuinely repeatable: **2+ entries, no land drop, no Land's Edge
discard, no pre-tap.** The first two are once-per-turn resources; a pre-tap names a *specific
untapped copy*, which the second iteration's board no longer has.

### `defer` — the one new primitive

```js
segmentParts(plan)   // an entry with `defer` starts a new segment; everything after it follows
```

Both features need exactly this, for the same underlying reason: **an engine Plan cannot hold the
whole block twice.** The pre-existing repeat split only covers sources the engine tags `repeatable`
(no `{T}`, no sacrifice — `ActivateBlink`, and the Drain/ExileTop perm abilities). A `{T}` draw outlet
like Mariposa's `{5}, {T}: Draw a card` is deliberately **not** tagged, because it genuinely cannot go
twice — *until a blink untaps it*. So `[draw, blink] × 3` is unexpressible without `defer`, and
`repeatBlock` marks the first entry of each repetition after the first:

```
[M, B, M*, B, M*, B]   ->   3 segments, each "cast=Mariposa…;blink=Emiel…@10*1"
```

`defer` is a statement about the **queue**, not about the card — which is why it is a flag on the
entry rather than another engine-published predicate. No entry the viewer built before this feature
carries one, so **every recorded line partitions exactly as it always did** (verified: all 1907
main-phase lines across 307 references still reconstruct).

Two implementation notes that are load-bearing rather than tidy:

* **The deferred scan starts at index 1.** A deferred entry at position 0 is already the head of the
  segment being built. Scanning from 0 cut there, produced an **empty** first segment, and
  `dropFirstSegment` — which peels by removing exactly the entries `segmentParts()[0]` names — then
  removed nothing and the commit chain spun forever on the second iteration. Caught by the new
  `checkLineMacros` layer before it ever ran in a browser.
* **The tail recurses as-is; entries are copied only by `repeatBlock`.** `dropFirstSegment` peels by
  object **identity**, so `segmentParts` must return the caller's own objects, while two repetitions
  must not *share* one (both would vanish when the first segment committed).

### The `*` full-order pin is now scoped to the committing segment

`plan0.actions` describes the one line being committed, so `applyAccepted`'s multiset test compares
it against **that segment's** entries rather than the whole queue. This made no difference while every
multi-segment plan was a bonus land drop (extra segments carry no actions) or a stack of one repeated
ability (one action, so the `acts.length >= 2` guard already declined) — but a macro iteration is a
genuine multi-action line, and unscoped the test compares 2 actions against all 6 queued entries,
fails, and drops the macro back to **enumerator order**. Which is precisely the defect
`human-line-order-as-is.md` exists to fix, re-introduced one layer up.

## The wrinkle: an untap between iterations must serve the CONTINUATION

`EtbUntapLands` ranks candidate lands by raw per-tap yield. Between two macro iterations that is the
wrong currency, for the same reason the existing `MTG_UNTAP_C_FIRST` note gives: the continuation
needs **specific pips**, and a high-yield land of a colour it does not want is surplus. If the next
queued action wants `{C}` or `{U}`, untapping a double-Overgrowth `{G}` land serves nothing.

### Why it has to be DECLARED, not inferred

`g_line_unpaid_cost` already carries rest-of-**line** coloured demand and the tap-ahead reads it. But
a macro's continuation lives in the **next committed segment** — a different process invocation, a
different plan. Nothing inside the engine can see it. The viewer knows it exactly (it is the rest of
the queue), so it says so.

### The token

```
need=<COLOURS>        e.g.  need=UC
```

It rides the **same `--cast-order` full-order list** the `tap=` token does, for the same three
reasons (`viewer-manual-tap-pay.md`): that list is the human's declared sequence, it is passed on the
**commit** path (`--validate-line` is not), and it is recorded verbatim per main-phase decision in
the reference trace and reconstructed verbatim by `test/viewer_protocol_check.py`. So a macro'd line
replays from a saved game with no new artifact field to keep in step.

Unlike `tap=` it carries **no position**: it describes the rest of the *queue*, not a point inside
this line, so it holds for the whole apply (`Plan::human_untap_need` → `HumanUntapNeedScope` →
`g_human_untap_need`, bound in `ApplyPlanDirect` beside `LineUnpaidCostScope`).

An unknown letter is **ignored**, not rejected — a demand is a *preference*, so the worst a typo can
do is ask for less than the human meant. That is the opposite of `tap=`, where guessing is the
failure being fixed, and the difference is deliberate.

### Where the viewer gets the colours

`entryPips` reads what the **engine published**, never a card name:

| entry | source | field |
|---|---|---|
| a board ACTIVATION | its enumerated action | `cost_pips` (new, `src/main.cpp`) |
| a hand CAST | the hand card | `cost` (the existing display string) |

**Pips only, never the generic half** — any source pays generic, so it constrains no untap, and
folding it in would make every activation "demand" everything and fire the promotion on noise. A
hybrid pip (`{G/W}`) is **skipped rather than guessed**: it demands neither colour specifically.
Emiel's blink is a bare `{3}` and therefore contributes nothing, which is the honest statement that
it does not care which land untaps.

### The promotion is BOUNDED to one pick

Modelled exactly on the starved-`{C}` promotion (`a6cc7155`), which is the same shape hardcoded to one
colour and a board-inferred sink. Post-sort, post-cut, pre-untap:

1. Net the declared demand against the float — a colour already in the pool is **not** demand.
   (Without this a standing `need=` would re-divert a pick every iteration and bleed yield for mana
   already held.)
2. If any land in the chosen top-`count` set already serves the unmet demand, **do nothing**.
3. Otherwise swap the **best serving land below the cut** for the **worst member of the chosen set**.

At most **one** pick moves, and only when the chosen set covers none of the unmet demand. It is a
preference, never a refusal: if no land below the cut can serve, the yield order stands. That bound
is what makes it safe to default ON.

`MTG_UNTAP_DEMAND_TRACE=1` prints one stderr line per diversion — the promotion is otherwise
unobservable from outside the process, since "the demand moved this pick" and "the yield order
happened to pick it" leave the same board.

## Byte-identity

Every engine site is gated on `HumanUntapDemandEnabled() && HumanPlayActive()`, and the search never
writes `Plan::human_untap_need`, so rollouts, autonomous play, the regression digests and every saved
reference are byte-identical by construction. `human_untap_need` is deliberately absent from
`PlanSignature` and `PlansEqual` for the same reason `human_pre_taps` and `human_action_order` are:
it is not part of the search's plan space, it is an instruction riding on top of one.

The autonomous executor branch in `AIEngine::TakeTurn` deliberately does **not** bind the scope: a
human-pinned plan never reaches it (the external-chooser path applies through
`TurnSolver::ApplyPlan` → `ApplyPlanDirect`, which binds it), so binding a provably-zero mask there
would be dead code on the one path that must not move.

`cost_pips` and `makes_clue` are additive fields on the decision JSON, which only the claude-play /
viewer path emits and no digest folds.

## Guards

| layer | what it pins |
|---|---|
| `test/viewer_client_check.js` (`testClueFuseOption`) | the pair collapses to **one** option in BOTH states (so neither pops a modal); the survivor is the right flavour; the default is ON; the pref round-trips through `localStorage` (it must outlive a game *and* a server restart); a non-investigate source is untouched; clicking a fused Investigate off takes its deferred crack with it. The only layer that can see any of this — linebuild knows nothing of options or pickers, and the committed line is identical either way |
| `test/viewer_linebuild_check.js` (`checkLineMacros`) | `repeatBlock` expands to N committable segments with the right `defer` flags; `dropFirstSegment` converges; a deferred entry carries its whole block; an **un**deferred plan partitions exactly as before; the fused pair encodes as two segments and removes as one unit; the `need=` token is a stable, order-independent, deduped set and is empty for a pip-less continuation |
| `test/viewer_line_macros_check.py` (in `test/viewer_checks.sh`) | `need=C` diverts **exactly one** pick and says so in the trace; `need=U` on an already-served board diverts **nothing**; no declaration diverts nothing; `MTG_UNTAP_LINE_DEMAND=0` is a real off switch; the investigate half really creates a Clue and the deferred crack is enumerated on the frame it produces; a repeated block's second iteration validates against the frame the first produced |
| `test/scenarios/edf_fused_clue_*.json` (in `test/scenarios.sh`, gated by `regression.sh`) | both halves of the fused gesture validate on a synthetic board, and the one-line form is **illegal** — the tripwire for the deferral |

## Deliberately not done

* **Repeating a block that contains a land drop, a Land's Edge discard or a pre-tap.** Each is either
  a once-per-turn resource or names a specific untapped copy that the next iteration's board no
  longer has. The control is simply not offered for such a block, rather than offered and then
  rejected.
* **A `need=` derived from anything but the queue.** Full generality would mean the engine inferring
  a continuation it cannot see. The macro case — where the continuation is known *exactly*, because
  the human just queued it — is covered; a continuation the human has not queued is not a
  continuation, it is a guess.
* **Demand beating yield by more than one pick.** The bounded form is the measured-safe shape
  inherited from the starved-`{C}` rule. A full re-ranking (untap a 1-yield `{U}` land ahead of a
  3-yield `{G}` one, repeatedly) **trades mana for pip type** and needs measuring rather than
  asserting — recorded here as unmeasured, not shipped, exactly as `MTG_UNTAP_C_FIRST`'s own note
  records its stronger form.
* **A scenario fixture for the untap promotion.** `--scenario` takes only a path and runs autonomous
  play, so it can reach neither `--cast-order` nor the external-chooser path that reads it. Driving
  it would mean either adding a test-only env override for `Plan::human_untap_need` (a flag whose
  only caller is a test) or teaching `RunScenario` about the human chooser. Covered by
  `viewer_line_macros_check.py` against a live board instead.
* **Undoing a macro as one gesture.** Each committed segment is its own step. `Clear` drops the whole
  *queued* remainder, and undo rolls back the committed segments one at a time as it always has —
  they are grouped under the existing `auto:` marker, so a step back rolls the declared line rather
  than half of it.
