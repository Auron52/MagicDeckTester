# Confound-gate leak — what is fixed, and what is still owed

The leak itself is found, fixed and guarded; the full record is
[fivecolour-bottoming-cause.md](fivecolour-bottoming-cause.md) §7k. This file is the short list of
work that the fix **implies** and that nobody has done yet, parked here rather than in an agent's
memory because it outlives any one session.

## Done (2026-09-17, commit `14afc53e` + the pipeline follow-up)

* `MTG_CONFOUND_BOTTOM=3` — mode 1 plus re-deriving `shuffle_salt`/`shuffle_salt_search` after the
  bottoming decision. Closes the gap from −0.0730t (5.8 se) to +0.0000t (±0.0135).
* Mode 1 left **byte-identical** so every historical measurement stays reproducible.
* `test/confound_gate_check.sh` — a property test of the instrument, with an empirical noise floor.
* `scripts/mullgen.sh` now runs its bottoming gate at mode 3, so **new decks are judged fairly by
  default**; `mulligan-profile.md` and `exhaustive-keep-policy.md` say mode 3 is the gate.

## Owed

### 1. Re-measure §7d-bis under mode 3 — it is doubtful, not retired
Its level check (scorer `V` vs realised win turns, split by which policy chose the cell) read realised
turns out of **mode-1 games**, i.e. through the leak. The lookahead-arm half — "the model is
pessimistic by +0.067t on the lookahead's picks" — is about the size of the leak (0.073t) and is
plausibly nothing but the leak. The table-arm half — "the model flatters its own picks by 0.077t",
the "optimizer's curse against the simulator" — involves a **blind** arm and may well survive.
Until someone re-runs `logs/fc_audit/level_check.py` against mode-3 games, **do not quote either half
as established.** Cheap: three 1000-game arms (~15 min) plus the existing scorer output.

### 2. Decide whether mode 3 should simply BE mode 1
Right now the fair gate is opt-in at the call sites. That is deliberate — flipping mode 1's meaning
would silently change what every historical `MTG_CONFOUND_BOTTOM=1` number referred to. The
alternative is to make mode 1 re-salt and rename the old behaviour (e.g. `=4` for
"draw-order-only, legacy"), which removes the footgun of someone typing `=1` out of habit and
re-introducing the bias. **Needs the user's call**, since it is a reproducibility-vs-footgun trade.

### 3. Audit the other call sites that still pass mode 1
`scripts/mullgen.sh` is fixed. Still on mode 1, and each needs a decision (they are mostly historical
records that should probably stay pinned for reproducibility, but should SAY so):
`test/keepmodel_burn_confound.sh`, `scripts/attic/dragonstorm_r40_finish.sh`, and the numbers quoted
in `hinata-profile-generation.md`, `auras-mulligan-profile.md`, `hinata-mulligan-profile.md`,
`keepgen-bottoming-HANDOFF.md`, `confounded-bottoming-gate-failures.md`, `learned-d0-policy.md`.

### 4. Re-check the decks that FAILED a confounded gate
Passing under mode 1 is safe (the bar was ~0.07t too high), so no adopted profile needs revisiting.
**Failures are the suspect set**, and any deck sent for regeneration or shipped bottoming-off on the
strength of a mode-1 failure deserves a mode-3 re-measure before that verdict stands.
`confounded-bottoming-gate-failures.md` (Dragons, Mirrorwing) is the place to start — those were
diagnosed as starved sub-tables, which may well be right, but the verdict came from the leaky gate.

### 5. The blind bottomer quantises its K-sample mean to an INTEGER
`AIEngine::BottomCards`: `win_turn[j] = (acc + K/2) / K`. So `MTG_NC_BLIND_BOTTOM_K` buys precision
that is immediately rounded away — candidates within half a turn collapse into ties. It made
`MTG_NC_BLIND_BOTTOM` unusable as a *cell-regeneration* instrument above K≈10 (the reason the K=30 arm
in `logs/fc_cells` was abandoned rather than finished). Harmless for the NC play policy it was built
for, which only needs a ranking. Fix behind a flag (keep a parallel `double` for ranking) if anyone
wants to use blind-K as a measuring instrument again.

## The transferable lesson

**A 1-sample estimator beating an N-sample one is an instrument bug until proven otherwise.** Two
weeks of hypotheses were built on top of that anomaly — six tested, five refuted, one adopted — and
the anomaly was the measurement. The probe that settled it took one run, because it compared two arms
that were *the same procedure at the same sample count*, differing in exactly one thing: at m=1 no
legal-subset table is built, so `lookahead` and a 1-sample blind bottomer differ only in which future
they evaluate. When a comparison needs a model to interpret, find the comparison that doesn't.
