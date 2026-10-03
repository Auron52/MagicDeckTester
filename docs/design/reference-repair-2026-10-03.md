# Repairing the 8 failing reference games (2026-10-03)

**USER RULING 2026-10-03, and it refines CLAUDE.md's "references are COMMIT-ONLY":**
*"there should not be failing references. We should repair them."* and, when told a reference may
never be overwritten: *"I don't agree with that. If what is in them changes, then we should change
the reference to reflect that. However, the win turn and intended line must remain the same."*

So the protected thing is the user's **play** — the win turn and the intended line — not its
**encoding**. A reference whose stored indices/option labels no longer describe the engine's menus
MAY be re-recorded, provided both invariants are verified explicitly. CLAUDE.md's blanket
"never overwrite or delete one" wording should be updated to say this; the "only the user decides"
clause is satisfied, because this is the user deciding.

**STATUS 2026-10-03, after `ffdaaa96`: 4 of the 8 are REPAIRED** (both contract-fails, both Auras
board-diverged), by resolving picks **by name** rather than by stored index — the direction the user
approved: *"I'm okay with changing the references to use names more as it makes a bit more sense for
this kind of thing, though we might still want indices when duplicates are involved."* No file under
`references/` was written; the TOOL was repaired. Both invariants hold on every repaired game (win
turn and intended line unchanged). The remaining 4 need a ruling or a re-play, not a tool fix.

**QUEUED, at the user's request** (*"let's queue it up until after the generation I suppose then"*):
the full 442-reference sweep. Validated so far: the 2 MDFC references and all 14 KittyEquipment v2
references. The sweep is deferred because **the sweep itself is what competes for the box** — see
"The checker's parent process is unbounded" below, which is the finding that forced the design.

## The tally

`test/regression.sh --regression` reference-reproducibility step, `--strict`:

```
84 ok, 350 repaired, 1 play-drift, 1 shuffle-dead, 3 board-diverged, 1 enum-gap,
0 mull-drift, 2 contract-fail   (442 refs)
```

Against the last recorded baseline this is **better** on one axis — mull-drift 10 → 0 — so the 8
failures are the whole of the work.

**The 350 "repaired" are PASSING and were deliberately left alone.** The harness's own legend says
*"Not a regression; re-save to make it permanent"*, so re-saving them is sanctioned — but that is
350 rewrites of user-owned data which the user did not ask for, and freezing them against today's
engine only buys permanence until the next engine change.

## Triage, by what the failure actually is

| # | reference | class | outcome |
|---|---|---|---|
| 1 | `KittyEquipment/v2-puresteel-hammer/claude_s2_gi1` | contract-fail | **REPAIRED** `ffdaaa96` — win_turn 3 reproduces |
| 2 | `KittyEquipment/v2-puresteel-hammer/claude_s8_gi7` | contract-fail | **REPAIRED** `ffdaaa96` — win_turn 4 reproduces |
| 3 | `Snow/claude_s4_gi3` | board-diverged (TAP STATE only) | OPEN — needs a ruling |
| 4 | `Auras/claude_s3_gi2` | board-diverged (MDFC face) | **REPAIRED** `ffdaaa96` — win_turn 4 reproduces |
| 5 | `Auras/claude_s12_gi11` | board-diverged (MDFC face) | **REPAIRED** `ffdaaa96` — win_turn 7 reproduces |
| 6 | `Mirrorwing_Dragon/v2-instigator-entrance/claude_s51_gi50` | play-drift T4 → T5 | OPEN — **re-record FORBIDDEN** (win turn moved) |
| 7 | `Mirrorwing_Dragon/v1-twinflame-anger/claude_s29_gi28` | shuffle-dead | OPEN — only re-playing restores it |
| 8 | `Hinata2/claude_s1_gi0` | enum-gap | OPEN — pre-existing, upstream hidden state |

The four repaired ones were all the SAME defect wearing two faces: **a pick resolved by stored
index where it should have been resolved by name.** #1/#2 needed the index to be in range (which
cost 3.22 GB); #4/#5 needed the index as a tiebreaker that a summary change had silently retired.

The four still open each need a per-case ruling on whether the engine change was intended; none is
a tool defect.

## 1 & 2: the contract-fails are `std::bad_alloc`, and the index is the fragility

Reproduced directly:

```
CONTRACT-FAIL KittyEquipment/v2-puresteel-hammer/claude_s2_gi1.json: engine error after 8 picks (rc=1)
Error: std::bad_alloc
```

`claude_s2_gi1` is seed 2 / gi 1, **win_turn 3**, 13 decisions. Its recorded picks:

| decision | turn | chosen index | nplans in the file |
|---|---|---|---|
| 7 | 3 | 36,854 | 201 |
| 8 | 3 | 93,305 | 201 |
| 9 | 3 | **419,900** | 201 |
| 10 | 3 | 45,926 | 201 |
| 11 | 3 | 54,022 (`equip all free (13) → Sram, Senior Edificer`) | 201 |

**The file stores 201 plans but the picks are six-digit.** Those were never menu clicks — they are
HAND-ASSEMBLED lines (decision 11 is literally the equip-all-free button), and the engine recorded
where each line happened to sit in the *full* enumeration. Recording a positional index for a line
the human built by hand is the actual defect: any enumeration change shifts it, and resolving it
forces the entire list into memory.

**Why that is 3.22 GB.** The viewer valve bounds **positions** (`pcap` 65,536) and is correctly
armed; one position yields several plans, so the frame still materialises **~420k plans**. Measured:

```
sizeof(TurnSolver::Plan) = 376    (9 std::string / std::vector members)
sizeof(Action)           = 416    (and Action NESTS std::vector<Action> breakpoint_casts)
```

`MTG_PLAY_PLANS_CAP` (default 200) caps only what is **emitted**; `plans` is fully materialised
before that, and `test/viewer_protocol_check.py` deliberately runs **uncapped** so it can
content-match a recorded index. Hence 420k × (376 + n·416 + nested + 9 string allocs) → over the
checker's `MTG_REPLAY_AS_CAP_MB=4096` `ulimit -v`.

**Do NOT raise the 4 GB cap to make this green.** That cap exists because the unbounded version
killed the user's session (`docs/design/claude-play-unprune-blowup.md`).

### The three fixes, and what each risks

1. **Shrink `Action` / `Plan`.** Pure memory, zero behavioural change, no index drift. `Action` at
   416 B with a nested `std::vector<Action>` is the dominant term. Also pays for itself twice over:
   container churn is 16.7% of wall (`free-equip-dominance-collapse.md` §9d) with ~5.7% of it
   building/destroying the per-plan action vector 29.7M times, and this is the documented likely
   source of viewer lag on wide equipment boards. Biggest win, most work.
2. **Bound retention while continuing to COUNT.** Indices stay exact by construction (the counter
   runs over the same enumeration), memory becomes O(n_emit). But the ranking that selects the shown
   200 then sees a truncated list, degrading the menu on exactly the rich frames where it matters —
   and the user has already rejected menu degradation once (`viewer-valve-ate-equip-actions`). Avoid.
3. **Record a hand-assembled pick by CONTENT, not index.** Permitted by the ruling above; the
   content is ALREADY in the file (the "chosen extra" entry is why `nplans` reads 201 not 200); and
   the harness already content-resolves — `viewer_protocol_check.py`'s `--emit-resolved` prints a
   `resolved` pick stream and `test/viewer_validate_check.js` consumes it precisely so it replays
   "the content-resolved stream rather than the recording's raw positional indices". Cheapest, and
   it removes the fragility instead of paying for it.

**Recommended: 3 for the references, 1 for the engine.** They are complementary and 1 is worth
having on its own merits. 2 is the one to leave alone.

### What was actually done (`ffdaaa96`), and why it was neither 1 nor 3 as written

Option 3 assumed the recorded CONTENT had to be added. It did not: **every plan in a reference
already carries its full content**, and the writer re-emits a chosen plan that sits beyond the
display cap, so the chosen line is always present (that is why `nplans` reads 201, not 200).
`find_plan` was already content-first with the index as a duplicate tiebreaker. So the fragility was
never the recording format — it was that **the checker needed the recorded INDEX to be in range**,
and uncapped emission was how it arranged that.

A replay does not read the menu; it reproduces one line the reference already names. So the fix is
to stop enumerating a menu for a replay at all:

* **a reduced plan-space bound for replays** (`MTG_VIEWER_PLAN_CAP_POSITIONS`, 8192 vs the viewer's
  65,536, overridable as `MTG_REPLAY_VALVE_POSITIONS`). The recorded content still resolves —
  `93305 -> 19595` — and peak child RSS falls from 3.22 GB to ~450 MB;
* **a pinned retry** for a reference the bound alone cannot replay: the recorded line's own card
  names into the valve's keep set via the existing `--full-enum` side channel.

Cost: stored indices shift more often, so references report `repaired` rather than `ok`. Under the
user's own rule that is cosmetic (the win turn and the line are what must hold), and 350 of 442 were
already in that class.

### The checker's parent process is unbounded — the finding that forced the design

`MTG_REPLAY_AS_CAP_MB` wraps each replay CHILD in a `ulimit`. Nothing wraps the Python parent, and
the parent is the bigger consumer: with emission uncapped it holds a wide frame's ~300 MB of
decision JSON as a string and then as parsed objects, **per thread**. Measured 2026-10-03: at
`--threads 4` the checker process reached **5.7 GB RSS and took this 23 GB box to 0 GB available**,
with the 32-thread mulligan generation running on it. The generation survived only because the
sweep was killed.

**This is on the ordinary regression path, not a corner.** `test/regression.sh` sets `MODE=regression`
as its DEFAULT (so even a `--deck=`-filtered run triggers the sweep, unfiltered, over the whole
corpus) and runs it at `VPC_THREADS=$(nproc)` — 32 here. That is the same shape as
`docs/design/claude-play-unprune-blowup.md`, which is why **raising the ulimit is forbidden** rather
than merely unattractive.

### Why the pin is held back rather than applied everywhere

`--full-enum` is not a neutral pin. `ApplyFullEnum` (main.cpp) **re-enumerates the frame a second
time** under `viewerplancap::PinScope` and emits a `search_gap` play event asserting the search
failed to offer the line. On a genuine override both are honest. Applied to all 442 references it
would double the sweep's enumeration work and write a false search-failure event into every frame.

**The clean version is a neutral, replay-only pin channel in the engine** — the same `PinScope`
applied to the PRIMARY enumeration, no second pass and no event. That is the one piece of this that
wants an engine change, and it is held behind the generation (`build/Release`).

### The retry fires on ENUM-GAP too, which makes the loud class stricter

A tighter bound drops more groups, so it can take a recorded line out of the menu — reported as
"a previously-offered plan is no longer enumerated". Pinning separates the causes exactly: a line
the **valve** dropped comes back when named; a line the **enumerator** no longer produces does not.
So a gap that survives a pin is now known to be the engine and not the bound. That is attribution,
not masking, and it is what makes the reduced bound safe to apply by default.

### The MDFC repair, because the cause is the reverse of the symptom

`Auras/claude_s3_gi2` and `claude_s12_gi11` were reported as board-diverged, and **the engine's own
fix created them.** Before `SummarizePlan` annotated the back face, a Pathway's two faces shared one
summary, `find_plan`'s summary tier returned BOTH, and `recorded_index` picked the recorded face
correctly. After the annotation the legacy un-annotated summary matches the FRONT plan *uniquely*,
so the tiebreaker is never consulted and the replay silently committed the manabase to {G},
starving every downstream {W}{W} cast — surfacing frames later as a board divergence.

Resolved by name out of the reference's own record: a reference does not store the face it chose,
but it stores the BOARD at every later frame, and a land that entered as its back face sits there
**under the back face's name**. `mdfc_face_intent` is three-state — back name / front / **unknown** —
because defaulting an unknown to "front" would rewrite a reference whose face cannot be read.

## 3, 4, 5: board-diverged — these need a RULING, not a repair

Content resolution did not merely mis-index; the recorded plan is **not enumerable at all**, because
an upstream decision moved the board. So re-recording cannot fix them — the question is whether the
upstream change was intended.

* `Snow/claude_s4_gi3` — *"board differs (TAP STATE only): ref-only ['Snow-Covered Island'] vs now
  ['Scrying Sheets']"*, `nplans 2->1`. A different source tapped for the same cost. Smells like an
  intended mana/tap-order heuristic change (cf. `MTG_SNOW_LOOK_COLOR`), in which case the reference
  wants re-playing.
* `Auras/claude_s3_gi2` — ref-only `['Boulderloft Pathway']` vs now `['Branchloft Pathway']`.
* `Auras/claude_s12_gi11` — ref-only `['Boulderloft Pathway']` vs now `[]`.

**Both Auras cases are the SAME CARD'S TWO FACES** (`Branchloft Pathway // Boulderloft Pathway`, an
MDFC — the reference's own decision 1 lists `mdfc_backs: ['Boulderloft Pathway', ...]`). So the
engine's land-FACE choice changed. That is one mechanism explaining two of the three, and it is the
first thing to check: a face choice is a real play decision, so if the new face is worse this is a
regression, and if it is better the references want re-playing.

## 6: play-drift is the only one that breaks the invariant

`Mirrorwing_Dragon/v2-instigator-entrance/claude_s51_gi50`: *"replay won=True win_turn=5 vs ref
won=True win_turn=4; 9 decision(s) the ref predates answered by engine default (e.g.
('main_phase', 1, 'pre_main', None)<--1(pass))"*.

The win turn moved, so **re-recording is forbidden by the ruling** — a re-save would launder a lost
turn into the baseline. But the lost turn may not be a regression either: the harness answered **9
decision points the reference predates** with engine defaults, so the T5 may be the substitutions
rather than the engine. Resolve by answering those 9 as the recorded line implies (or having the
user re-play the game), and only then compare. This was already OPEN in
`versioned-references-were-ungated`.

## 7, 8: not agent-repairable

* `Mirrorwing_Dragon/v1-twinflame-anger/claude_s29_gi28` — shuffle-dead: a mid-game reshuffle moved
  the draws (`nplans 133->63`, hand ref-only `['Mountain']`). The harness itself says *"only
  re-playing can restore this game"*. This is the documented ACCEPTED drift class.
* `Hinata2/claude_s1_gi0` — enum-gap, *"the hand is IDENTICAL yet a previously-offered plan is no
  longer enumerated"* at a Soulfire Eruption target; upstream hidden state diverged. Pre-existing
  and verified not-new once already (rebuilt in a worktree, tally identical).

## Working constraint while the mulligan generation runs

`scripts/mullgen.sh run` is ONE long-lived `build/Release/mtg-analyze`, and the driver later spawns
**`build/Release/mtg`** for its validation A/Bs and its `run_regression` step. Rebuilding
`build/Release` mid-run would therefore make the validation measure a different engine than the
generation produced — a silent integrity break, which is the sharp end of
`hold-edits-not-just-compute`. So:

* source edits and commits: **safe** (the run reads binaries and data, not git);
* `cards.json` edits and `./build.sh` into `build/Release`: **held** until the run finishes;
* engine work goes in a **`git worktree` with its own build directory**, which is unaffected.

Verify idleness by executable, not process name: `ls -l /proc/*/exe | grep -i mtg`.

**AND THE REFERENCE SWEEP ITSELF IS HELD, not just builds** — which was not obvious until it was
measured. The sweep is not CPU-polite work that happens to be slow: at `--threads 4` it took the
box to 0 GB available (above). So while a generation holds ~14 GB, a corpus sweep is a threat to it,
and this is the one tool whose *default* thread count is `$(nproc)`.

## QUEUED WORK (after the generation finishes)

1. **The full 442-reference sweep** under `ffdaaa96`, on an idle box. Compare against the recorded
   baseline `84 ok, 350 repaired, 1 play-drift, 1 shuffle-dead, 3 board-diverged, 1 enum-gap,
   0 mull-drift, 2 contract-fail`. Expect `0 contract-fail`, `1 board-diverged` (Snow only), and
   `ok` to fall toward 0 as stored indices shift under the reduced bound — that last is the
   expected cost, not a regression, but any NEW `play-drift` or `enum-gap` is a real finding.
2. **`test/viewer_validate_check.js`** has not been run at all since the `valve` plumbing went in.
3. **A neutral replay-only pin channel in the engine** (see above) so the pin stops riding
   `--full-enum`'s re-enumeration and `search_gap` event.
4. **Shrink `Action` / `Plan`** — option 1, still worth having on its own merits (`Action` is 416 B
   with a nested `std::vector<Action>`; container churn is 16.7% of wall, §9d). The reduced bound
   removes the crash, not the underlying cost.
5. **Bound the checker's PARENT memory**, or stop uncapping emission now that resolution never
   needs a beyond-cap index in range. `MTG_PLAY_PLANS_CAP=0` is the remaining reason one frame
   costs the Python process hundreds of MB.
6. **The four open references**, each needing the user's ruling:
   * `Snow/claude_s4_gi3` — is the new tap choice (Scrying Sheets over Snow-Covered Island, first
     diverging at the `dig` frame) an intended mana-ordering improvement? If so the reference wants
     re-playing; if not it is a regression to chase. Note the recorded game still wins on T5 in the
     reference, and the replay never gets there.
   * `Mirrorwing_Dragon/v2-instigator-entrance/claude_s51_gi50` — T4 → T5. Re-recording is
     FORBIDDEN by the user's rule (the win turn moved). But it is only 8 recorded decisions and the
     harness answered **9 ref-predating frames** with engine defaults, so the lost turn may be the
     substitutions rather than the engine. Resolve those 9 as the recorded line implies before
     concluding anything.
   * `Mirrorwing_Dragon/v1-twinflame-anger/claude_s29_gi28` — shuffle-dead, the documented accepted
     class. Only the user re-playing it restores it.
   * `Hinata2/claude_s1_gi0` — enum-gap at a Soulfire Eruption target; pre-existing and already
     verified not-new once (rebuilt in a worktree, tally identical).
