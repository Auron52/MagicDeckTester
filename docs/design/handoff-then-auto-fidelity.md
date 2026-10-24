# Hand-off fidelity: `--choices-then-auto` from turn 1 does not reproduce the batch search

**Status: OPEN (found 2026-10-24, not investigated).** Found while building the reference review
(`scripts/ref_review.py`), which hands the shipped search your board at the start of each turn of a
reference game.

## The defect

`scripts/ref_handoff.py` states its own fidelity contract: handing the search the board at **turn 1**
(an empty pick prefix, `--choices '' --choices-then-auto`) must reproduce the reference bench's win
turn, which comes from the batch path (`mtg --batch` with the same seed, game index and
`force_mulligan`). On the 97 references where the search beats the human, it does not hold for 7. In
each one, the hand-off from turn 1 wins **later** than the batch search:

| reference | batch (bench) | hand-off from T1 |
|---|---|---|
| `burn/claude_s21_gi20.json` | T5 | T7 |
| `FiveColour/claude_s13_gi12.json` | T4 | T8 |
| `Fungus/claude_s10_gi9.json` | T4 | T5 |
| `Goblins/claude_s24_gi23.json` | T3 | T4 |
| `Hinata2/claude_s8_gi7.json` | T7 | T8 |
| `Melira_Pod/claude_s3_gi2.json` | T4 | T5 |
| `suboptimal/burn/claude_s18_gi17.json` | T4 | T5 |

These are **not** explained by:

- the reference's side-channel pins (`--force-attackers`, `--tap-pref`, `--cast-order`): the same
  result with them removed;
- the recording-rule flags (`--legacy-*`): the same result with them removed;
- a per-process cache cap (`MTG_MEM_BUDGET_MB`): the same result unset.

A different case, `CritterLifegain/claude_s6_gi5.json` (batch T4, hand-off T5), **is** explained: it
replays under `--legacy-single-damage-step`, and without that flag the hand-off wins T4 like the
batch. That is a rules change since the game was recorded, not a path defect. The review flags such
games itself (`rules` in `review.json`).

## Leading hypothesis (untested)

The claude-play entry point sets `MTG_HUMAN_PLAY=1` for the whole process
(`src/main.cpp`, just before `RunClaudePlay`): *"Human play executes EXACTLY the committed plan -- no
auto re-solve after a draw, no auto-dig, no auto Land's Edge"* (ApplyPlanDirect, AIEngine's
external-chooser segment loop). After `kHandBackToSearch` the human choosers are uninstalled, but the
env flag stays set. If the autonomous search's executor reads it too, the search after a hand-off
executes its plans literally: no re-solve when a mid-turn draw changes the hand. That would cost turns
exactly on decks that draw mid-turn (burn's and Goblins' card draw, Hinata2's cantrips, FiveColour,
Fungus).

Test: clear (or never set) `MTG_HUMAN_PLAY` once `then_auto` hands back, and re-run the 7 repros below.
Each should then match the bench. The flag must stay on for the human's own frames.

## Repro (each is one game, seconds)

```
MTG_PLAY_PLANS_CAP=0 MTG_VIEWER_PLAN_CAP_POSITIONS=8192 build/Release/mtg decks/Goblins/Goblins.cod \
  --claude-play --seed 24 --game-index 23 --max-turns 8 --profile decks/Goblins/Goblins.profile.json \
  --choices '' --choices-then-auto --force-mulligan 1:58          # wins T4; the bench wins T3
```

Every affected game's exact command is in `logs/ref_review/<ref dir>/<game>/review.json`
(`ladder[0].cv_cmd`, rebuilt by `python3 scripts/ref_review.py build`).

## What it costs until fixed

- **The reference review:** for these games the hand-off ladder is measured on a weaker search than
  the bench's, so its lost-turn localisation and hint are unreliable. The review page shows a red
  banner on each one ("the hand-off from turn 1 wins Tx, the bench Ty").
- **`scripts/ref_handoff.py` / `ref_handoff_sweep.py`:** every hand-off verdict on these decks may
  understate what the search can do from a human board.
- **Promotion** (`scripts/ref_promote.py`) is not affected: it replays the search's logged line in
  the play engine and gates on the outcome.
