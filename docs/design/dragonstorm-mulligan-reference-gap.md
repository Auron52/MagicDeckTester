# Dragonstorm reference-match gap: the PLAY is perfect, the MULLIGAN heuristic is the gap (deferred)

2026-07-22. Testing the user's 9 saved Dragonstorm references (`references/Dragonstorm/claude_s*_gi0.json`,
hand-played ground-truth with recorded `win_turn`) against the engine's autonomous play. Per the user,
the win turn is the signal: play needn't match move-for-move, but a win-turn mismatch means an issue.

**Status: DEFERRED by user ("just report, don't change yet"). Profile + GT left unchanged. This doc
records the diagnosis + the measured quick fix so the mulligan work can be picked up later.**

## Result: 5/9 match, 4/9 the engine is slower — and it is 100% a MULLIGAN problem

| seed | human (ref) | engine autonomous | forced to human's hand |
|---|---|---|---|
| s6  | T4 | T6 | **T4** |
| s8  | T4 | T5 | **T4** |
| s9,s10,s11,s12,s14 | — | match | — |
| s13 | T3 | T6 | **T3** |
| s15 | T3 | T4 | **T3** |

Forcing the search onto the human's exact opening hand (`--force-mulligan "<count>:<bottomed card nums>"`,
where count+bottom come from the reference's `mulligan` object) reproduces the human's win turn in **all
four** mismatches. So the search/play is not the issue — given the same hand it matches the human exactly.
The gap is entirely the **keep (mulligan) decision**.

The 4 mismatches are **pre-existing** — the pre-Irencrag-fix binary gives identical win turns, so the
execution-enforcement / cast-ordering work did not cause them.

## Root cause: no trained keep model + a curve check that miscounts a combo deck's mana

Dragonstorm has only `Dragonstorm.profile.json` (no `.value.json`, no exhaustive `.keepmodel`
*(2026-09-03: both now exist — 5e646e77 shipped the R=40 keepmodel and a .value.json; the
`curve_check: none` quick fix is still unadopted, the profile still reads two_drop)*), so mulligan
uses the **default heuristic** (`AIEngine::KeepHand`). Its `curve_check: two_drop` rejects any hand with
`land_count < 2` (`AIEngine.cpp` ~L534) — counting **only lands**, not rituals or Lotus Bloom. For a
ritual/Lotus combo deck this is wrong: e.g. s6's initial 7 was `Apex of Power, Mountain, Desperate Ritual,
Ruby Medallion, Seething Song, Lotus Bloom, Utvara Hellkite` — 1 land but Lotus Bloom + 2 rituals = plenty
of mana, a clear keep (human kept it → T4). The heuristic mulliganed it and **kept a 5-land, no-payoff
flood** instead (→ T6).

## Measured quick fix (not adopted): `curve_check: none`

Setting `curve_check` to `none` in the profile:
- **References:** matches s6 and s8 (both → T4), speeds s14 (T5 → T3), regresses none.
- **Aggregate A/B** (500 games, seed 20000, loss-penalized avg): d3 4.8920 → **4.8700** (−0.022),
  d5 4.8960 → **4.8660** (−0.030). Net-positive, not just reference-fitting.

It does **not** fix s13/s15. Those need aggressively mulliganing a keepable-but-slow 7 to dig for a fast
combo (s13: engine keeps a 2-Dragonstorm/Apex/Kolaghan 7 → T6; human mulled to 4 → T3) — a higher-variance
strategy the crude heuristic can't express. That is the exhaustive **mulligan-profile** stage's job (see
`.claude/skills/mulligan-profile.md`), which is expensive and commit-bound.

## If picked up later

1. Cheap, measured win: flip `curve_check` to `none` (or add a combo-aware mana count so rituals/Lotus
   Bloom satisfy the two-drop check), then rebaseline Dragonstorm smoke+regression GT (mulligan change →
   all Dragonstorm rows move; overnight too). Re-verify the references afterward.
2. For s13/s15 and full coverage: generate an exhaustive keep profile per the mulligan-profile skill
   (late, on a frozen commit; the raw sidecar's `commit` fingerprint gates cross-machine pooling).
3. Reproduce any reference: `--force-mulligan "<count>:<nums>"` (spec from the ref's `mulligan` object)
   forces the human's exact hand; drop it to see the engine's autonomous keep.
