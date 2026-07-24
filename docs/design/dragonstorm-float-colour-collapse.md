# Dragonstorm float-colour collapse + feasible-aware ritual-drop

Two enumeration prunes that make Dragonstorm's exhaustive mulligan keep-generation
tractable by attacking the single longest rollout (the "atom" that caps every
scheduling gain). Captured worst rollout before this work: **~31.7 minutes** for one
d3/b10 game; after: **~11 seconds** (~90–180×). See
`memory/dragonstorm-degenerate-game-capture.md` for the capture + measurements.

## 1. The blowup (measured, not assumed)

`MTG_BRANCH_STATS=1` on the slow replays (drop off, single-thread) attributed the
enumeration to the **"add N mana of ANY ONE COLOUR" float-colour fan-out**:

- **Apex of Power** (`impulse_exile`) and **Lotus Bloom** (`SacForMana`) each emit
  ONE cast/sac Action *per candidate colour* (`ChosenFloatColorCandidates`).
- Candidate colours = every colour with a pip in the AP's hand / **library** / gy /
  battlefield costs. This deck is red-primary with exactly two off-red cards:
  **Dragonlord Kolaghan `{4}{B}{R}`** and **Karrthus `{4}{B}{R}{G}`** — so the set is
  `{R,B,G}` whenever those sit in the library (≈ always).
- Result: a `~3^(#Lotus) × 3(Apex)` colour factor. Top driver **Apex of Power**
  reached **589,824 plans in a single `EnumeratePlans` node**; Desperate Ritual ~295k.

This is NOT ritual *waste* (the payoffs are in hand) — it is legitimate
mana-production enumeration, but almost entirely redundant off-colour branches.

## 2. Float-colour collapse (HEURISTIC — provider-owned, not byte-identical)

Only consider colours we can actually **use this turn**. A floated colour empties at
end of turn, so an off-colour is worth a sac only when a spell that needs it is
castable *this* turn. In this deck the only off-colour castable spells are the two
haste Dragons.

- **Apex of Power → RED only** (`DecisionProvider::ImpulseFloatColorRedOnly`,
  DragonstormProvider = true). Apex's mono-red chain never needs another colour, and
  one colour can't fund a multicolour card by itself.
- **Lotus Bloom → RED unless a HASTE creature castable this turn demands an
  off-colour** (`RestrictSacColorsToHasteAndRed`). The scan is over `ap.hand`, which
  **includes Apex-staged exile cards** (`m_is_staged`), so it covers both "haste
  Dragon in hand" and "haste Dragon castable from an Apex exile". Red weakly dominates
  (pays generic + red pips), so it is always kept and listed first.
  - ⇒ Karrthus/Kolaghan in hand → `{R,B(,G)}`; otherwise → `{R}` (collapse).

Both open to the full candidate set under `MTG_UNPRUNED(SacColor)`. Base provider
returns false → unchanged (library-scoped, all-colour) for every other deck; and no
other current deck has `SacForMana`/`impulse_exile`, so this is inert elsewhere.

Deliberately **dropped** (user call): the surplus-Lotus-→-black safety and the
one-black/green-needs-black micro-rules — real-player judgement is they don't matter
enough to justify the cost/complexity (they'd also re-introduce off-colour branches).

### Measured

- Speed (rollout time, captured slow hands): **25–180×**; the 31-min atom → ~11 s.
- Avg-win-turn (full regression, 5 Dragonstorm seed/depth cases): **net +0.001 turns**
  (`+0.003, +0.003, -0.003, +0.004, 0.000`) — NEUTRAL, the primary metric.
- Changed games (audited): mostly same-win-turn cosmetic line changes; a handful of
  ±1–3-turn shifts that are combo-timing / Apex-exile / Dragonstorm-shuffle variance,
  plus one genuine +1 (gi227) offset by faster games. GT rebaselined via
  `--accept-with-regressions`.

## 3. Feasible-aware ritual-drop (BYTE-IDENTICAL companion)

`DropRitualGroupsIfNoPayoff` (TurnSolver.cpp; called after
`CapGroupsBySituationalRank` in both Solve's odometer and EnumeratePlans) removes
ritual (`ritual_float > 0`) groups + independent Lotus sacs from the odometer when the
hand cannot reach ANY payoff this turn — provider-gated (Dragonstorm) + the same
`MTG_UNPRUNED(payoffprune)` toggle as the late payoff-prune, opt-out
`MTG_NO_RITUAL_EARLY_DROP`.

**Byte-identity lesson (the expensive one):** the engine's affordability is
**NON-sequenced** — `consider()`/`eval_and_push` credit a ritual's GROSS float as WILD
and unconditionally (`pool + Σgross ≥ Σcost`, simultaneous; plus the Rite-of-Flame
`gy_self` triangular bonus), with no check that each ritual is castable in sequence.
So the ONLY byte-identical feasibility bound is the matching non-sequenced net
`feasible_net = pool + Σ(gross−cost) + gy_self*(gy_self-1)/2`. A tighter "bootstrap
ideal cast-order" bound drops ritual+payoff plans the engine keeps at 0–1 lands → NOT
byte-identical (this caused a Dragonstorm smoke regression during development). The
old `MTG_MANA_FEASIBLE` sequenced `ManaPruneBound` variant was removed for the same
reason. Validated: smoke 21/0 + regression 35/0 byte-identical **before** the colour
collapse landed.

Benefit is bimodal (occasional ~100× on rollouts that never reach a payoff; ~neutral
otherwise) — kept as a zero-risk guard; the colour collapse is the dominant win.

## Diagnostic: `MTG_KEEP_REPLAY`

`ExhaustiveKeep.cpp` reconstructs and runs exactly ONE keep-rollout from a captured
`slow.log` line (`MTG_KEEP_REPLAY="<hand>"` + `_R` + `_PD`), byte-identical to the
gen, prints `[replay] DONE win_turn/elapsed`, then exits. Point `MTG_EQUIV_CACHE` at
the capture's cache + `MTG_COMMIT` for a fast discovery hit. This is the tool that
made every fix above measurable in isolation.
