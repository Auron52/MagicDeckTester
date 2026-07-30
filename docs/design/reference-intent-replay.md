# Reference stability: intent replay for the protocol check (2026-07-30)

**Outcome:** `test/viewer_protocol_check.py` now replays references by INTENT (plan content +
engine defaults) instead of raw positional indices. On the same binary the same 140 references
went from `105 ok / 34 play-drift / 1 mull-drift` to
**100 ok / 38 repaired / 0 play-drift / 0 enum-gap / 2 shuffle-dead** — the two survivors being
the old Hinata2 refs (s2_gi1, s12_gi11; saved 07-02/07-08, pre-stable-shuffle), i.e. exactly the
already-accepted mid-game-shuffle class and nothing else. No engine change was involved — the
"drift" was a measurement artifact.

## Trigger

After pulling the accelerant-ordering/mana-bound commits (`cb806c8..e97c85a`), the viewer checks
reported 34 play-drift. A pre-pull A/B showed 33 of 34 were IDENTICAL pre-pull — the pull changed
exactly one reference (Dragonstorm s30_gi29, an index shift, its recorded line still wins T4).
The user (correctly) rejected "stale references" as an explanation and asked for root causes.

## Why index replay lied

A reference records each pick as a positional index into that step's plan list, PLUS the full
plan description (`summary`/`land`/`casts`) and the full decision frame. The positional stream
breaks — silently, in both directions — whenever the engine evolves:

1. **Inserted decision points.** A decision type added after a reference was saved (`target` for
   Searing Blood/Blaze 6fb1de6d, `land_entry` fc3d1c61, `sacrifice`, `replicate`, re-prompt
   main_phase frames from #12) consumes the NEXT recorded pick, sliding the whole stream by one.
   12 of burn's 16 references "did not terminate" this way — their lines were perfectly intact.
2. **Enumeration growth/reorder.** A wider or reordered plan list re-points a recorded index at a
   different plan ("pick 44 out of range", or worse, a coincidentally-valid wrong plan).
3. **False greens.** The desynced stream can accidentally reproduce `won/win_turn` — three
   references (Anti-Lifegain s8_gi7, Auras s3_gi2/s12_gi11) read "ok" while replaying a
   scrambled line. Index replay under-reported AND over-reported.
4. **The mulligan "8->0 cards" false positive.** The old check compared `me.hand` of the engine's
   first frame (a mulligan frame keeps its hand at TOP level; `me.hand` is empty) against the
   ref's post-draw T1 hand — reporting a mulligan divergence that never happened.

## The intent-replay walk (`check_reference`)

Aligns on decision IDENTITY `(type, turn, source)`, never stream position:

- **Aligned frame** → re-anchor the recorded pick by CONTENT: exact `summary` match first, then
  `(land, sorted casts)`. Among EQUAL matches prefer the recorded index — plans can be visibly
  identical yet distinct (an MDFC land's two faces emit the same summary; only the index
  separates {G}-Branchloft from {W}-Boulderloft, and taking the first match silently flips the
  face and starves downstream colour pips: Auras s3_gi2's Daybreak Coronet).
- **Inserted frame** (the ref predates the decision point) → consume NO recorded pick; answer a
  `main_phase` frame with PASS (the ref's own decisions already express everything the human cast
  that turn — a plan answer would cast cards the recorded line needs later, e.g. Anti-Lifegain
  s26_gi25's Invigorate), and every other type with the frame's own `heuristic_default`/
  `ai_choice` (= the unattended engine, per decision-indexed-choice-protocol.md).
- **Recorded plan not found** → root-cause before reporting: hand DIFFERS at that frame →
  `shuffle-dead` (a mid-game reshuffle moved the draws; the accepted class per
  antilife-reference-shuffle-alignment.md — only re-playing restores it). Hand IDENTICAL →
  `ENUM-GAP` (the engine stopped offering a plan for the same state — the loud category), with a
  caveat when defaulted upstream answers could have diverged hidden state.
- NO casts-only/any-land fallback tier: measured on s8_gi7 it compounds substitutions into a
  false loss. A shuffle-dead reference is dead; the checker says so instead of approximating.

Categories: `ok` / `repaired` (reproduces after index repair or default answers — re-save via
the GUI to make it permanent) / `play-drift` (replays to a DIFFERENT outcome — real behaviour
change) / `shuffle-dead` / `ENUM-GAP` / `mull-drift` (like-for-like hand comparison). Exit code:
contract failures always gate; `--strict` also gates play-drift + enum-gap (the engine-moved
categories). shuffle-dead and mull-drift are accepted classes and never gate.

## What today's sweep actually contained

- **The pull was innocent**: 1 reference affected (index shift), repaired by content matching.
- **burn (12+2), treasure_hunt, most others**: inserted-decision desync → all repair.
- **Auras s3_gi2 / s12_gi11 / Hinata2 s1_gi0**: MDFC face-duplicate mismatch (checker defect,
  fixed by recorded-index preference) — NOT an engine regression. Chased to ground: the
  "missing" Coronet plan was a face-flip starving {W}{W}; `--validate-line` verdicts and a
  3-commit bisect confirmed enumeration never changed.
- **Anti-Lifegain s8_gi7**: first read as shuffle-dead (T4 draw Overgrown Tomb -> Windswept
  Heath), actually the SAME duplicate-index class: its fetch-land plans emit duplicate summaries
  (one per fetch TARGET), and taking the first match cracked a different fetch — different
  reshuffle, different draws. Recorded-index preference restores the recorded fetch target and
  the whole line reproduces (won T5).
- **Genuinely shuffle-dead (2)**: Hinata2 s2_gi1 (post-Ponder T2 draw Mystic Monastery ->
  Island) and s12_gi11 (T5 draw Ponder -> Ornithopter). Both predate the stable-shuffle/
  numbering work (saved 07-02/07-08) — the known, user-accepted class. Re-playing by hand is
  the only restoration; keeping or pruning them is the user's call (the 07-22 antilife audit
  pruned its equivalents).

## Stability contract (how references stay valid from here)

1. **A new decision type MUST carry its unattended default in the frame** (`heuristic_default`
   or `ai_choice` = exactly what the engine does when nobody answers). The checker then answers
   predated frames with it and old references survive by construction. Prefer a keyed
   side-channel (decision-indexed-choice-protocol.md) for new HUMAN choices; either way the
   default-in-frame rule holds.
2. **Plan identity is content + index-for-duplicates.** If two plans are semantically different
   they should differ in `summary` or `land`/`casts`; where they legitimately cannot (MDFC
   faces), the recorded index is the tie-break — do not reorder duplicate groups gratuitously.
3. **Run `bash test/viewer_checks.sh` after touching the emitter, the enumerator, or
   tools/play/** — `repaired` is fine (re-save when convenient), `play-drift`/`ENUM-GAP` are
   findings to investigate BEFORE re-saving anything (re-saving on top of a regression would
   bake it in).
4. **References are commit-only** (CLAUDE.md): the checker never writes them; repairs become
   permanent only when the user re-saves via the play viewer.

## Artifacts

- `logs/viewer_checks/run_e97c85a.log` — the misleading post-pull index-replay sweep.
- `logs/viewer_checks/protocol_PREPULL_bd4b367.log` — pre-pull A/B (proved the pull innocent).
- `logs/viewer_checks/protocol_final_e97c85a.log` — final intent-replay sweep.
- `logs/viewer_checks/probe_*.log` — per-reference stuck-frame dumps used for root-causing.
- `scripts/ref_line_replay.py` — now imports the shared `plan_key`/`find_plan` from the check.
