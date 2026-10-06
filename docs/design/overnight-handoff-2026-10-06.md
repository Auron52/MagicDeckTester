# Overnight handoff — 2026-10-06

This handoff is for the session that starts the overnight run on 2026-10-06. The user asked for an
overnight run in a NEW session. That session owns the choice of job. This file lists the candidates
and what must be true before any of them starts.

## Preconditions (check all of them before launching)

1. **The box is free.** One heavy job at a time, `--threads 20`. Check that no `mtg` or `cc1plus`
   is running with `pgrep -x mtg` and `pgrep -x cc1plus`. Never use `pgrep -f` in a wait loop: it
   matches its own command line.
2. **The primary checkout is current:** `git -C /workspaces/MagicDeckTester pull --rebase`, then
   `./build.sh`. The harness runs `build/Release/mtg`.
3. **References are safe:** `python3 scripts/check_references_safe.py` exits 0.
4. **Results go somewhere durable.** Copy the tier output to `logs/durable/` as soon as it lands.
   `/tmp` and `~` (other than `~/.claude`) are container overlay.
5. Two agent branches were being wound down at handoff. Check whether they landed on
   `phase-1-2-deck-analyzer` or were parked:
   * `viewer-combat-swap` (pushed, NOT landed; base e1171a1d): human-play only. A main-phase
     Arcanum Wings → Colossification (host-tapping Aura) swap onto a would-be attacker is applied
     automatically in combat, after attackers are declared, when its {2}{U} is payable without
     attackers' mana. Old references replay with the old timing via a recording stamp. 460/460
     unit tests pass. Search wins 131/131 of the "Wings + Colossification is lethal" states.
     **LAND IT FIRST, before the overnight run.** The user will not record Bruna references until
     it is in. Steps: rebase onto the tip, `./build.sh`, run smoke + regression (must be
     byte-identical: human play only), run the viewer checks, push, watch CI (Windows), restart
     the viewer from the primary checkout. Full steps: `docs/design/viewer-combat-swap-status.md` (on that branch).
     Logs: `logs/durable/viewer_cswap_2026-10-06/`.
   * `viewer-combat-swap-engine-parked` (pushed, parked, AWAITS THE USER): a CR 508.1f fix. The
     engine taps attackers only at combat damage, so an attacking Birds or Pilgrim could pay the
     combat swap, which is illegal. The branch also keeps a mana creature home when the swap needs
     its mana. Cost: only Bruna moves (smoke +33 turns, regression +23). 21 of 23 slower smoke games
     were wins that relied on the illegal payment; the other 2 recover at a higher budget. Search
     wins 158/158 automatic-win states. The same bug affects firebreathing:
     `docs/design/attackers-tapped-at-declaration.md`. The user decides whether it lands.
   * `wish-heuristic`: Glittering Wish candidate shortlist (user spec: at most 2 Auras, the
     highest-power Aura first; a cheap Aura only as a searched second candidate when no
     cheat-into-play path exists; no duplicate Bruna, which is the fallback/enabler; Troyan when
     low on mana). It had held-out counterexamples against `MTG_WISH_FULL_WIDTH` under diagnosis.
     It ships only once the proof is clean. On a counterexample, fix the ranking; never fall back
     to branching. See `docs/design/glittering-wish-heuristic.md` if it was written.
   If a branch landed, the overnight measures it. If it was parked, the overnight measures the
   tree without it.

## Overnight candidates (owed; the user decides which)

* **Bruna overnight GT (owed).** The Bruna / bruna2hg rows exist in the overnight tier, but no GT
  has been accepted for them. `regression_tiers` stays PARTIAL until it is, and the rule is all
  three tiers or none. Run `bash test/regression.sh --overnight --deck=<bruna keys>`, inspect, then
  `--overnight --accept --deck=...`. Every difference needs a verdict.
* **`MTG_ATTACK_BODY_TAP_ORDER` held-out A/B.** It is on in human play only. The candidate is
  adoption for autonomous play. Mirrorwing v2 `s51_gi50` replays at T4 under the flag. Adoption
  needs a held-out A/B with every deck net ≤0 and a user sign-off. See
  `docs/design/reference-gate-red-on-origin-tip.md` and `docs/design/mana-payment-rollback.md`.
* **FiveColour overnight GT.** It may be owed: check `regression_tiers` for the deck.

## Open user decisions (repeat them to the user; never settle them yourself)

* Bruna cast order and discard policy (`MTG_BRUNA_ORDER` off; `MTG_BRUNA_BUCKET_DISCARD` on,
  PROPOSED). The user will do these later.
* Value-leaf power input. Recommendation: add a corrected input before generating Bruna's value
  leaf. The Bruna value leaf → mulligan sequence waits until play settles (cast order and discard).
* Whether bruna2hg's +1 must clear the net ≤0 bar as its own per-row bar.
* A Selesnya determinism flicker that appears only in full pooled suite runs. It is not from the
  Bruna branch and needs an owner.
* Legacy worktrees `/home/vscode/wt/{bruna,reffix,refsafe,hinata-perf}` are on overlay. Remove
  them with `scripts/safe_worktree_remove.sh` once nothing in them is unpushed.

## Housekeeping

* `test/ref_bench.json` in the primary checkout has an uncommitted refresh: only the `src` hashes
  moved, to a tree that is not HEAD. It is a regenerable cache, not a reference. Regenerate it at
  HEAD or leave it; do not commit the stale-hash version.
* The viewer runs from the primary checkout only:
  `env -C /workspaces/MagicDeckTester PLAY_HOST=0.0.0.0 MDT_REFERENCE_BACKUP_DIR=/home/vscode/.claude/mdt-reference-backups ./play.sh --no-open`.
  Restart it after the viewer branch lands. Commit any new references before you restart or kill it.
