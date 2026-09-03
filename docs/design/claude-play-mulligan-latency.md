# claude-play startup latency — exhaustive keep sidecar parse (ROOT-CAUSED + fixed)

**Status (2026-07-17):** ROOT-CAUSED and fixed for the claude-play path. Split out from the
`land_entry` viewer work so the two don't get conflated. This is a **performance** finding, NOT
mulligan-profile generation and NOT caused by the `land_entry` change (smoke byte-identical).

## Symptom

A single `--claude-play` launch spent **~68–82 s before it even returned the first decision**, and
because the stateless-replay protocol relaunches the binary *per decision*, a whole game walk was
`O(decisions) × ~70 s` — making the viewer hang on "New game" and making Stage-5h sweeps /
in-session live verification impractical.

## Root cause (perf, 2026-07-17)

Profiled one launch (`perf record -g`, Anti-Lifegain). **91 % of wall time is
`AttachExhaustiveSidecar` → `CachedExhaustiveKeep` → `LoadDeckProfile` → `DeckProfileFromJson`**,
i.e. `nlohmann::json::parse` of the deck's **exhaustive keep sidecar** (a large JSON;
`json::parse` alone is 60 %+). It is loaded at startup — even without `--profile` (the sidecar path
is auto-derived) — purely for the opening mulligan keep decision. The actual keep eval, `KeepHand`,
and gameplay are negligible (< 1 %). So it was never the "mulligan keep-eval"; it is
**deserialization of the keep table**.

## Fix (shipped)

**claude-play skips the exhaustive keep sidecar by default.** `main.cpp` gates
`AttachExhaustiveSidecar` on `!claude_play || --exhaustive-keep`. The deck's **base** `.profile.json`
still loads (small/fast) and supplies the static keep (`min_lands`/`max_lands`/`stop_at`), so
**mulligans still happen and still toss 0-land / flood / all-land junk** — they're just not the
exhaustive-optimal table. `--exhaustive-keep` opts back into loading it when the optimal mulligan
hint is wanted. Autonomous / batch (`--batch` → `BatchRunner`) / scenario paths are **unchanged**
(they always load it), so regression fingerprints are byte-identical.

Measured: default claude-play launch **~70 s → < 1 s**; `--exhaustive-keep` reproduces the ~74 s
load (confirming the sidecar is the entire cost).

### Why skipping is correct here (user, 2026-07-17)

claude-play in this process is a **play-verification tool**: can a human/Claude *out-play* the
engine AI, or find bugs in its play? ("play" = card implementation + AI heuristics + search.) If it
can, the engine's play has a gap to fix. Mulligan **optimality is irrelevant** to that — we only
need reasonable (not-junk) opening hands to test play on, which the static default keep provides.
And the governing principle: **"if play is fully reliable, so will the mulligan table"** — mulligan
quality is downstream of play, so the expensive keep table is exactly the thing you do NOT need
while still validating play. (This is also why mulligan-profile generation is the last,
user-initiated stage — see `deck-onboarding-hardening.md` "Pipeline ordering".)

## Deeper follow-up (deferred) — reuse the parsed sidecar between instances

**(Updated 2026-09-03: the parse-once binary cache WAS built — 26683d95, 2026-07-19,
`<sidecar>.bincache` in MulliganProfileIO.h, ~5x faster keep-hint launch; only the stateful
play server remains unbuilt.)**

If we ever want *sidecar-quality* mulligan hints in the viewer without the reload cost, the
per-launch JSON re-parse is the thing to kill (the stateless protocol reloads it every launch).
Options, unbuilt:
- **Parse-once binary cache:** on first load, parse the JSON and write a fast-loading binary blob
  (keyed by content hash); later launches `mmap`/load it in ms. Helps every deck with a sidecar.
- **Stateful play server:** hold the parsed `ExhaustiveKeepPolicy` in a long-lived process the
  viewer talks to, instead of relaunching the stateless binary per decision.
Neither is needed for play verification; only for a fast *and* optimal viewer mulligan experience.

## Non-goals

- Not a correctness bug. Not related to `land_entry`. Not mulligan-profile generation.
