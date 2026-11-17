# Red Elementals value-leaf handoff — 2026-11-17

> **CLOSED 2026-11-17 (Claude).** The run completed and the sidecar shipped (`nlfit`, d6/b20; moved to d6/b30 by the USER the same day). Red GT was accepted after attribution. The build-target change was kept (`de8284806`). Results: `analysis-Red Elementals.md`, "Value leaf and play setting (2026-11-17)". What follows is the historical handoff.

The mulligan profile is shipped. This note hands the in-progress value-leaf run to a Claude agent in **this primary checkout**. Read `CLAUDE.md`, `.claude/skills/value-leaf.md`, and the older `docs/design/red-elementals-analysis-checkpoint.md` before shipping. The old checkpoint contains superseded running-job notes; this file describes the current run.

## Live job — leave it running

- Branch: `phase-1-2-deck-analyzer`, frozen HEAD `24949783a`. Source fingerprint is in `logs/vlq_red_elementals/freeze.src`; play fingerprint in `freeze.play`. PGO+LTO engine is `build/PGO/mtg`.
- Main shell PID **2165089**: `bash scripts/valueleaf.sh run decks/Red Elementals`. Phase E.7 Python PID **2196133**; batch engine PID **2196184** when checked. Verify with `kill -0 2165089` and `ps -p 2165089,2196133,2196184 -o pid,ppid,stat,etime,comm` because child PIDs can change.
- Currently the E.7 play-setting grid: `logs/vlq_red_elementals/playset/grid.manifest.json`, **200 jobs × 1,000 games = 200,000 games**. At handoff, `grid.log` had 72 lines (header plus **71 completed jobs**). Monitor `wc -l logs/vlq_red_elementals/playset/grid.log`, `tail -5 logs/vlq_red_elementals/driver.log`, and `bash scripts/valueleaf.sh status 'decks/Red Elementals'`. The active Codex PTY session was 86982, but the OS PIDs and files are the portable handles.
- **Do not interrupt E.7 merely to hand off.** `play_setting_stage.py:run_batch` only reuses a *fully completed* grid and opens `grid.log` for writing on restart; an interrupted grid would repeat all 200,000 games. Do not launch a concurrent batch or rebuild against changing source. When the job ends, the shell will continue into E.7b trust recheck and E.6 skip calibration automatically.
- The shell job completing does not wake an agent. Claude must remain active or arrange its own explicit continuation, then finish the shipping gates below.

## Results so far

- Phase A: 2,500 games, 10,774 unique training rows. Phase B: 120-tree model, 1,569 leaves, heldout RMSE **0.3358 turns**. Staged model: `logs/eval/Red Elementals.value.STAGED.json`.
- Phase C0 chose K=none; H5's greedy-vs-1-ply comparison said “NOT ENGAGED (identical units -- stale engine?)”. Phase C matrix completed all 52 cells / 832 batches, zero intractable; H5 and V6–V8 each averaged **4.2975**. Risk gate was clean. Phase D nominated trust depth 5 for the *staged model*.
- Phase E (8 seeds × 1,000 games each): live **4.29288** turns; staged leaf **4.29250**, 0.773× work units; no-leaf escalation **4.29175**, 0.749×; no-leaf final-depth single pass (`nlfit`) **4.29188**, **0.699×**; emulation **4.29350**, 0.895×. All four passed the quality/cost rule. The staged model's trust ON/OFF comparison passed its bound, but the model itself was not selected.
- E.5 automatically selected **`nlfit`, the leafless final-depth single pass**, as the cheapest passing shape. Its uncommitted runtime sidecar is `decks/Red Elementals/Red Elementals.value.json` with `value_play: {ladder: single, leaf: none, alpha: relaxed}`. The learned model remains staged; do not describe it as shipped. Details are in `logs/vlq_red_elementals/DECISIONS.txt` and `m_red_elementals_shape.log`.
- The initial play-fingerprint smoke run completed. All three Red searched smoke rows matched accepted score/digest, but the **full pooled smoke suite reported 48 failing rows in other decks** after upstream engine commits. This is not a green full-suite result and needs attribution/separate handling after this sole-box generation finishes.

## Remaining work

1. Let E.7 finish, then let E.7b and E.6 finish. Read final `DECISIONS.txt`, `driver.log`, E.7 selection, trust recheck, skip allowance, and installed sidecar. If the script fails, inspect the failure. Re-running `MTG_BUILD_JOBS=5 bash scripts/valueleaf.sh run 'decks/Red Elementals'` is incremental for completed phases, but **an unfinished E.7 grid restarts in full**.
2. Apply the value-leaf skill's shipping gate to the final sidecar: Red smoke and regular-tier regression, per-game attribution/GT acceptance where play changes, and overnight if `value_trust_depth` moves. Check other-deck pooled-suite failures separately. Preserve the frozen play semantics during generation.
3. Review the one-line local `scripts/valueleaf.sh` change at line 876: `bash build.sh` became `bash build.sh release -- --target mtg`. It avoids compiling hundreds of unrelated unit-test targets for the freeze's Release play fingerprint. Keep or amend it based on the driver contract; include it in a commit if kept.
4. Record final measurements and adoption in the Red Elementals analysis ledger and checkpoint. Commit the chosen sidecar with `provenance` as the skill requires, relevant accepted GT and docs; run reference safety checks and push the shared branch per repo rules. Do not overwrite the user's unrelated local edits.

## Local tree at handoff

`git status --short`: modified `.vscode/settings.json` and `test/ref_bench.json` (pre-existing, user/other session; leave alone); modified `scripts/valueleaf.sh` (the one-line build-target change); untracked `decks/Red Elementals/Red Elementals.value.json` (E.5 installation, subject to later phases). Raw logs and staged model are gitignored. No Codex-to-Claude agent invocation was available here; the user will start Claude in this checkout.
