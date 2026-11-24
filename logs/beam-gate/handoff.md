# Escalation beam depth gate (2026-11-24) -- status / handoff

Branch `wip/beam-gate` (from origin/phase-1-2-deck-analyzer e8a2f7d64). Worktree `.claude/worktrees/beam-gate`.
Subagents never push the shared branch; this branch only.

## USER rulings (verbatim)
- 2026-11-24, on the depth-5-only value beam that made Hinata worse at d5 than d4: "It does sound weird that we act
  differently at depth 5, so I would be up for changing that for sure. The only question would be results I suppose. I
  do still have a slight preference for not having additional per-deck settings, but if we don't have a choice to get
  good results I could be convinced."
- Adoption bar, 2026-11-24: "I think I would be willing to accept something that helps a lot of decks, but doesn't
  help a few deck and then look for ways to mitigate the cost on the other decks without blocking the adoption.
  Naturally, if the only reason it is worse on those other decks are the specific seeds we can just move on. d0 is not
  very important at all as I said previously. The primary thing that matters is play settings, though we should also
  check whether any worse case is a result of clairvoyant play. We can ignore those." / "(until we can show it is
  clairvoyance, variance or fix it)" / "If play settings are super marginally better, but other settings are much
  worse it might be worth holding back as well, but usually play settings being better is enough." Also: report CPU
  per arm.

## What the beam is
`FullSearchLineHybrid` (src/ai/TurnSolver.cpp, "DEPTH-ADAPTIVE BEAM"): inside the HEURISTIC ESCALATION pass that
follows the value-leaf probe, nodes within `leafdepth` plies of the leaf expand only the first W plans
(`g_esc_beam_width`, cut in FSLineWin / FSLineTail). Value regime: the probe records per-node plan values
(`g_probe_val_recording`, which also turns off the strict FS cut and the DVS tail-skip in the probe) and the
escalation reorders by them before the cap. Static regime: cap by MoveOrderPlans order, no recording.
Regime choice (only when the deck's value_play block drives; otherwise AIEngine passes -1 = off):
- depth == 5: the deck's `value_play.beam_width` / `beam_leafdepth` (value-ranked; width 0 = off).
- depth == 3: static W20 ld1 for EVERY driving deck (overrides the deck's config).
- any other depth: off.
A fixed width is a prune at every budget, b0 included (not a price), so neither regime is budget-complete.

History: f517bb364 (2026-07-18) value-guided beam on antilife+hinata (W2->W3 ld2 with fresh 0.5, July engine; the
quality gain measured then came bundled with fresh0.5, now unconditional); 115333bd8 (07-18) made it depth-adaptive:
`depth >= 5` value, `depth == 3` static W20 ld1 (measured neutral+faster at d3 on 6 decks); eded828bc (07-19) narrowed
`>= 5` to `== 5` ("d6+ ... gated off so off-policy deep runs don't hit an unvalidated beam path"). d4 was a
deliberate unmeasured hole. Creature Giving got W3 in 6a9380998, Dragonstorm in ff55c9286.

## Where it is live at PLAY settings today
Driving decks with target_depth 5 and beam_width > 0: **Anti-Lifegain, Creature Giving, Dragonstorm** (W3 ld2).
Hinata: `beam_width: 0` (d5E0) -- identical to the key being ABSENT (parser default 0), so deleting the key changes
nothing. Every other driving deck plays at d5 with no width (off) or d6/d7 (off). The d3 static regime never fires at
a play setting (no deck plays d3); it fires on every driving deck's pinned d3 suite rows.

## Arms (one binary; heurarm per-job flags, default OFF => origin byte-identical)
- A: origin.
- B: `MTG_ESC_BEAM_NONE` -- no beam at any depth; beam_width and the d3 regime ignored (= deleting the code path).
- C: `MTG_ESC_BEAM_STATIC_ALL` -- static W20 ld1 at every depth for every driving deck; beam_width ignored.
(NONE wins if both set. The MTG_ESC_BEAM env research override still takes precedence over both.)
Identical by construction: A==B at play for every deck but AL/CG/DS (incl. Hinata); A==C at d3 (all decks); A==B at
d4/d6/d7. A "B5" variant (drop only the d5 regime, keep d3 static) = B's play rows + A's d3 rows, no extra games.

## Manifest
`logs/beam-gate/mk_ab.py` -> `build/scratch/beam-gate/ab/ab.json` (+ rows.json with the equivalences);
`logs/beam-gate/ab_report.py <outdir>` pairs per game vs A and sums cpu/units per arm from batch.out.
Seeds base 6,400,001 (fresh; 2.86M/2.87M are used elsewhere). Full: 35 driving suite decks, 137 rows, 510 jobs,
216,200 games, est ~432K core-s (suite cost table, which includes mulligan cost -- likely an over-estimate).
Core only (`--no-fleet`, the 4 named decks): 16 rows, 82 jobs, 39,400 games, est ~153K core-s.
NOT RUN (coordinator runs it under build/scratch/claude_box.lock).

## Sanity games
Release build of 493dd5f4e (src tree = origin + the two slots), `mtg-test` rc 0. Small batches, --threads 2, seeds
6900001/6900101 (not A/B seeds), `build/scratch/beam-gate/sanity/`:
- Anti-Lifegain play (d5 b20, `[play] source=value_play`) 2 games, A/B/C: identical digests AND units (374) -- these
  games ran NO escalation (MTG_ESC_JOB_STATS taken=0/0), so the beam had nothing to cut. AL d3 1 game A vs C:
  identical (expected by construction).
- Hinata play (d5 b30, emulated ladder) 1 game A/B/C: identical, 32,636 units each; no escalation either (the emulated
  ladder commits the probe's heuristic line). With beam_width 0 Hinata has no recording, so A == B by construction;
  C differs from A only where Hinata's escalation runs.
- Dragonstorm play 1 game A/B: identical (157 units).
- Creature Giving play 2 games (the wiring check): same avg 5.0 and digests in all three arms, but the escalation's
  units differ -- A (W3 value) 1,320, B (no beam) 23,628, C (static W20) 21,512; game units 43,447 / 65,755 / 63,639,
  cpu 1,149 / 1,686 / 1,623 ms. So both flags reach the gate, and on CG the W3 beam is a real CPU saving (B +51% on
  these 2 games). Whether B costs quality is the A/B's question.
