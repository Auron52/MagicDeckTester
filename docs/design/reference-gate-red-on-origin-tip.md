# The reference reproducibility gate is RED on `origin/phase-1-2-deck-analyzer`

**Status: OPEN defect, found 2026-10-04, NOT introduced by the finder.** Four of 442 saved
references no longer replay. One of them is a **play-drift**, which the standing user rule treats as
an engine bug rather than an aged-out reference.

## What fails

```
play-drift      Mirrorwing_Dragon/v2-instigator-entrance/claude_s51_gi50.json
                replay won=True win_turn=5  vs  ref won=True win_turn=4
                (9 decisions the ref predates answered by engine default)

ENUM-GAP        Hinata2/claude_s1_gi0.json
                recorded option ('label', ('Opponent (face)', 'You (face)', '1/1 Spirit Token',
                '1/1 Spirit Token', 'Hinata, Dawn-Crowned')) no longer offered at
                ('target', 6, None, 'Soulfire Eruption')  (noptions 16->16)
                "also absent with the recorded line PINNED by name, so this is the enumeration
                 and not the plan-space bound"

BOARD-DIVERGED  Snow/claude_s4_gi3.json
                recorded plan 'land=none; cast: Frost Augur' no longer enumerated at
                ('main_phase', 5, 'pre_main', None)  (nplans 2->1); hand identical, board differs
                (TAP STATE only): ref-only ['Snow-Covered Island'] vs now ['Scrying Sheets'];
                first divergence at ('dig', 5, None, 'Scrying Sheets')

shuffle-dead    Mirrorwing_Dragon/v1-twinflame-anger/claude_s29_gi28.json
                (the ACCEPTED class -- restore only by re-playing by hand; does not gate)
```

Tally: `74 ok, 364 repaired, 1 play-drift, 1 shuffle-dead, 1 board-diverged, 1 enum-gap (442 refs)`.
`--strict` fails on play-drift and ENUM-GAP, so the regression tier returns non-zero.

## It predates the branch's local work — measured, not argued

A clean `./build.sh` of **pure `origin/phase-1-2-deck-analyzer`** in a scratch worktree reproduces
the **identical** four references in the identical classes, with the identical tally. The
`references/` tree is byte-identical between that worktree and the integrated one
(`git rev-parse HEAD:references` matches), and the local commits touch nothing under `test/` except
one new unit-test file. So the only variable was the engine, and the engine that fails is origin's.

Reproduce:
```bash
git worktree add /tmp/origin-pure --detach origin/phase-1-2-deck-analyzer
cd /tmp/origin-pure && ./build.sh
MTG_BIN=/tmp/origin-pure/build/Release/mtg python3 test/viewer_protocol_check.py --strict --threads 24
```

## WHY IT WAS MISSED, which is the reusable part

**The reference gate runs in REGRESSION MODE ONLY.** `test/regression.sh` sets `VPC_RUN=1` only for
`MODE = regression` (`VPC_ALWAYS=1` forces it, `VPC_SKIP=1` suppresses it) — a deliberate 2026-08-17
user decision, because each replay is a `--claude-play` process with `MTG_UNPRUNED` set and running
it in all three tiers re-measured the same corpus three times.

The consequence is that **smoke is not a sufficient gate for anything that can move play.** Smoke
checks fingerprints; it never replays a human game. A change pushed on a green smoke, or on green
CI (which builds and runs unit tests + a determinism sample, not the reference corpus), cannot
reveal a reference drift. The repo's own `regression-cadence` rule — smoke AND regression before
commit — exists for exactly this, and the gap this file records is what skipping the second half
looks like.

## What is owed

* **The play-drift needs an engine root-cause.** The standing user rule (2026-08-27): *"repairing it
  is always acceptable, but the win turn must not change. If it does, there is something wrong"*, and
  *"things like 'draws diverged' are not going to cut it"*. A reference is hand-played, user-owned
  ground truth; losing its recorded win turn means realized play got **worse**. Do not re-save it.
  The sole exception is a recorded line that rode an engine bug, and that must be PROVEN to the user
  and signed off, never silently accepted.
* **The ENUM-GAP needs investigating before anything is re-saved** — the harness says so itself: the
  hand is identical yet a previously-offered plan is gone, i.e. the engine changed under the same
  state. The check already rules out the plan-space bound by pinning the recorded line by name.
* **BOARD-DIVERGED is tap state only**, so it is likely a payment/ordering divergence rather than a
  decision change — cheapest of the three to chase, and a plausible shared cause with the Snow
  work on this branch (`6fb10c80` play main 2 only on turns Jorn attacked, `9dc0a9b2` the MDFC as
  Jorn, `b01edca9` never enumerate Skred, `ecb422af` short-circuit UntapSecondMainLive). That is a
  LEAD, not an attribution — none of it has been bisected.

## Suspects worth bisecting first

The incoming work most likely to touch these three decks is the executor/search block on this
branch: `2e85e9fa` (finish a game the search has proven unwinnable; clamp the ladder; lazy leaf for
unlimited budgets), `0c1619e5` (never search again on a committed line or after a d8 b0 no-win),
`3de66a1f` / `e7e512d8` (the committed-line mirror), `60c168f8` (replay deferred acquisition
continuations at the deferred point), `88173a32` (acquisition deferral applies to SEARCHED decisions
only). Several of those change *when* the engine re-searches a committed line, which is precisely
the mechanism a reference replay exercises.

Bisect with the command above over that range rather than reading code — the repo's own lesson
(`work-units-not-bit-reproducible.md`) is that correct code reasoning reached the wrong conclusion
about a cache and even predicted the wrong sign.

## Not to be confused with

`docs/design/batch-run-to-run-nondeterminism.md` (open) is about **digests of budgeted batch cells**
being unstable run-to-run. This is different: a reference replay is a single-threaded, content-
anchored replay of a recorded human line, and the four verdicts here reproduced identically across
two independent builds — they are stable, not noisy.
