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

Status when this was written: diagnosed, **not yet repaired**. The engine half is blocked on the
12 h mulligan generation owning `build/Release` (see "Working constraint" below).

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

| # | reference | class | repairable by re-record? |
|---|---|---|---|
| 1 | `KittyEquipment/v2-puresteel-hammer/claude_s2_gi1` | contract-fail | no — crashes before resolution |
| 2 | `KittyEquipment/v2-puresteel-hammer/claude_s8_gi7` | contract-fail | no — same |
| 3 | `Snow/claude_s4_gi3` | board-diverged (TAP STATE only) | no — line not enumerable; needs a ruling |
| 4 | `Auras/claude_s3_gi2` | board-diverged (MDFC face) | no — same |
| 5 | `Auras/claude_s12_gi11` | board-diverged (MDFC face) | no — same |
| 6 | `Mirrorwing_Dragon/v2-instigator-entrance/claude_s51_gi50` | play-drift T4 → T5 | **no — breaks the win-turn invariant** |
| 7 | `Mirrorwing_Dragon/v1-twinflame-anger/claude_s29_gi28` | shuffle-dead | no — harness: only re-playing restores it |
| 8 | `Hinata2/claude_s1_gi0` | enum-gap | no — pre-existing, upstream hidden state |

Only #1/#2 have a known mechanism. The rest need a per-case ruling on whether the engine change
was intended.

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
