# One bounded treasure_hunt game cost hours — root-caused and FIXED (2026-08-06)

**Status: FIXED — by the heuristic-prune redesign alone.** The user's design ("the heuristic
decides the candidates and returns a list; all of those options are searched") is the entire fix:
treasure_hunt's ranking decides its shed outright, the repro game is **flat ≤0.7 s at every depth
d3–d8**, and the ranking (after two rule fixes it exposed) is **net BETTER than the whole-hand
searched fan the old ground truth embodied**. Ground truth rebaselined (user-accepted).

An interim **probe-trial depth pin** (`MTG_PROBE_ROLLOUT_DEPTH`, default 3) shipped first and was
then **REMOVED at the user's direction**: it meant "search different decisions at a different
depth" — a fidelity fork the search-with-heuristics architecture has nowhere else — and once the
redesign landed it was unnecessary (the flat curve holds without it; its one held-out deviation,
antilife s7007 gi=382, reverts to ground truth on removal). Probe trials inherit the full engine,
uniformly.

The hours-long game was a **product of two independently-adopted
features**, not a defect in either one, and not the mechanism the first investigation named. The
depth-pin fix restores a **flat depth curve at today's answers**:

| depth | pre-fix (default engine) | post-fix (default: pin 3, fan kept) | `MTG_PROBE_ROLLOUT_BP=0` pin 1 | `MTG_BP_SEARCH=0` (old engine) |
|---|---|---|---|---|
| d3 | 5.2–7.2 s | 7.1 s (pin ≥ depth: unchanged by construction) | 0.55 s | 0.44 s (but answers T4, worse) |
| d4 | 60.9–89.9 s | **7.2 s** | 0.58 s | 0.43 s |
| d5 | ~1300 s | **7.2 s** | 0.60 s | 0.41 s |
| d6–d8 | never finished | **7.1–7.2 s FLAT** | 0.58–0.68 s | 0.41–0.44 s |

Answer is T3 at every depth, in every arm. The legacy hatch (`MTG_PROBE_ROLLOUT_DEPTH=-1`)
reproduces the pre-fix cost exactly (88.6 s at d4). Repro game: `--seed 9010 --game-index 1`,
V arm, one core (full command at the bottom).

## The real root cause: probe rollouts × per-turn full-depth fan-out ladders

The **searched cleanup discard** (`MTG_SEARCHED_DISCARD`, adopted default-ON ~2026-07-08) labels
each discard candidate by playing a **full clairvoyant engine game** (`RolloutWinTurnFrom` →
`PlayOutFrom` → `TakeTurn` per turn). Each trial-game turn runs its own **full unbounded depth-d
ladder**. The **Land's Edge fire-count probe** does the same (2 trials per ambiguous activation).

When the probe was adopted, those internal searches were nearly free. The 2026-07-28 searched
breakpoint continuation then made **every enumeration fan out** (`BpSearchInRollouts()` default ON
— wave-0 variants at every ply and every rollout turn, waves walking all remaining ranks at every
node under an unlimited budget). A late-game refutation ladder went from ~500 nodes to ~18k+
(×12/ply). Neither feature regressed alone; the **product regime** — fat hands × unbounded ×
depth — was never measured:

```
cost ≈ (#discard candidates) × (#trial turns) × ladder(depth) × fan-out
     ≈ 20 × 5 × (×12 per ply)        on treasure_hunt, whose whole mechanism
                                      is ballooning its hand to 15–25 cards
```

Attribution evidence (all on the repro game):
* `MTG_SEARCHED_DISCARD=0`: d4 drops 60.9 s → **0.58 s**, answer unchanged (T3), and the curve is
  flat 0.46–0.49 s through d8 **with the full current engine** (breakpoint search, waves, all
  ranks, unbounded). The core committed search was never the problem.
* Per-decision profiling: the real turn-3 decision costs **370 nodes**; >99% of all nodes are
  decisions at turns 3–7 **inside `m_in_rollout` trial games** (e.g. "turn 6 pre: 18,546 nodes"
  in a game that ends at turn 3).
* The t6–t8 node mass (t7=138,274 at d4) is the trials' internal refutation ladders sweeping fat
  late-game states, not the committed search.

## The fix (commit this doc rides with)

One concept: **probe trials are estimators — their cost must not scale with the matrix depth.**

1. **Depth pin** in `RolloutWinTurnFrom`: guarded trials pin their per-turn lookahead to
   `MTG_PROBE_ROLLOUT_DEPTH` (**default 3**, chosen empirically — see the sweep below;
   `-1` restores the old inherit-full-depth behaviour = the legacy arm). Inheriting the matrix
   depth is what coupled probe cost to `--depth`. This is the piece that flattens the curve.
2. **`TurnSolver::ProbeRolloutGuard`** — RAII scope set at the two probe sites (searched cleanup
   discard, Land's Edge fire count), read by the pin above and by the optional fan suppression:
   `MTG_PROBE_ROLLOUT_BP` (**default ON** — trials keep the searched-breakpoint fan-out, matching
   the engine's own leaf rollouts / `BpSearchInRollouts`; `=0` drops it inside trials for another
   ~10×, verified to cut bp resolutions 1.6M → 190).

**The fidelity sweep** (smoke suite; only TH cases ever moved, every other deck byte-identical in
all arms):

| trial regime | TH smoke vs GT | repro-game cost (flat in all arms) |
|---|---|---|
| pin 3 + fan (**DEFAULT**) | **byte-identical, 27/27** | 7.1–7.2 s |
| pin 1–2 + fan | gi=68 d3: 7→8 (+0.007 on that case) | 0.63–0.68 s |
| pin 1–3, fan suppressed | gi=38 3→4 (d3+d5), gi=68 7→8 (d3) | 0.55–0.60 s |
| pin 3, fan suppressed | gi=38 3→4 only | ~0.6 s |

So gi=38 needs the fan inside trials (the trial's own TH casts must play searched continuations)
and gi=68 needs trial depth ≥ 3. The default takes the strongest-fidelity flat point; the cheaper
arms remain one env var away for bulk generation where ±0.007 on one case is acceptable.

**Scope guarantees:** the REAL game's committed search never runs under the guard — declared-depth
completeness is untouched (the user's bar: "no result unreachable within our depth"). The
keep/bottom **generator** (`RolloutKeepWinTurn`) and the un-guarded probe sites (lookahead
bottoming, searched vial/echo upkeep) never set the guard and are byte-unchanged — keep-profile
fingerprints are not invalidated.

**Behavioural scope:** at the defaults the smoke suite is byte-identical to ground truth (27/27).
In principle a pin-3 trial can still label differently than an inherit-full-depth trial at engine
depths > 3 on other seeds — the regression tier (disjoint seeds) is the wider check, and GT
acceptance stays the user's call.

## What the first investigation got wrong (kept so it is not re-trodden)

* **"Breakpoints consume depth plies inside a turn" was not the mechanism.** True as a statement
  about `depth - 1` recursion, but the ladder stops at the first verified win, real decisions are
  cheap (t1=3, t2=10, t3=370 nodes), and probes-off is flat to d8 with the full fan-out engine.
* **The `EnumerateBreakpointPlans` memo thrash (cleared wholesale at 8192) is real but worth ~8%**
  (misses 178k→68k with a big cap; d4 65.8→60.9 s). Refuted as the restorer. Cap is now
  overridable (`MTG_BP_ENUM_CACHE_CAP`) with probe counters (`MTG_BP_ENUM_PROBE=1`:
  hits/misses/clears). A proper eviction remains a small follow-up.
* **Wave-0 `min(W, n)` emission** (Opus fix 1): root-only waste, post-apply dedup already catches
  the duplicate's rollout; small constant. Not implemented.
* **Order-sensitive `BuildSimKey`** (hand and battlefield folded in order, so play-order
  permutations never share a memo entry): real, measured **~10%** + 45% fewer distinct bp states
  via the `MTG_CANON_SIMKEY=1` experiment (kept in-tree, default OFF — not provably byte-identical
  because tie-breaks read vector order). Worth a separate look, not the exponent.
* **Waves** are a ~3.5× constant on this game (`MTG_BP_WAVES=0`: 26.8 s at d4, still ×9.7/ply).
  `improved=0` over 1.07M wave candidates here is one game, not a verdict on the feature.
* **A counter on one `return` of a multi-return function lies** (the "2.4 branching" that was
  really 40.6) — and **a stale comment lies harder**: `FSLineWin`'s "deeper nodes keep the cheap
  greedy continuation" predated `BpSearchInRollouts()` defaulting ON and mis-aimed the entire
  first pass. Both are corrected in-tree.
* Ruled out by measurement: the STAGED value model (91.2 vs 89.9 s), `MTG_FS_NOWIN_CACHE`
  (92.5 s), cycling/Fiery Islet as breakpoint sites (class fires zero times), copy-level plan
  duplication (name-keyed), nesting depth (`max-at=2`; a `MTG_BP_NEST_MAX` cap was written and
  reverted — measure an axis before capping it).

## Follow-ups

* **Suite A/B + GT rebaseline** for the probe fix (user decision).
* **Lookahead bottoming and searched vial/echo upkeep probes** share the same product structure
  (unguarded): bottoming fires once per mulligan game, vial/echo per upkeep on Vial decks. Extend
  the guard with its own digest check if V-cell generation on those decks shows the same tail.
* Name-dedup of discard candidates (same-name discards are identical trials): lossless ~2.5× on
  fat hands, now second-order.
* `EnumerateBreakpointPlans` eviction (don't clear wholesale) — ~8%.
* The three cutoff prunes preserved on `wip/th-bp-cutoff-prunes` (1.63× on the OLD cost shape;
  re-evaluate need at all post-fix).
* `MTG_TT_NOWIN_CACHE` (default OFF, `1612bc0`) — measured ~8%, decide separately.

## Exact repro

```
MTG_VALUE_MODEL=1 MTG_VALUE_PROFILE=logs/eval/treasure_hunt.value.STAGED.json \
MTG_VALUE_MIN_DEPTH=0 MTG_VALUE_STARTGATE_ALPHA=8 \
build/Release/mtg decks/treasure_hunt/treasure_hunt.txt \
    --profile decks/treasure_hunt/treasure_hunt.profile.json \
    --seed 9010 --game-index 1 --games 1 --max-turns 8 --threads 1 \
    --ignore-play-profile --depth 4
```

Legacy arm (reproduces the regression): add `MTG_PROBE_ROLLOUT_DEPTH=-1`.
Probe scripts and arm logs: `logs/vcell_probe/fable_arms*.{sh,log}`, `fable_sweep*.{sh,log}`.

## The heuristic-prune redesign (user's design, supersedes the pin for the cleanup discard)

The user's objection to the pin — "it goes directly against my design: search with heuristics" —
led to the real finding: **the TH cleanup ranking was commissioned precisely to prevent this case
and was never wired as a prune.** `ChooseDiscard` fanned a probe rollout over the ENTIRE hand and
used the ranking only as a tie-break, despite `DecisionProvider.h` already documenting the
intended contract ("returning ONE index decides the discard with no branch").

The design as adopted: **the heuristic decides the candidates and returns a list of some size; all
of those options are searched — no width knobs anywhere.** The base ranking returns the full hand
(generic decks unchanged, byte-identical); TH returns its top pick (measured: top-2/3 recovered
almost nothing over top-1). The full ordering survives as `CleanupDiscardFullRanking` for the
multi-card consumers (Land's Edge pitch, retrace costs).

**Why the ranking's pick lost 4 of ~1800 games to the fan, per the user's mistake-vs-clairvoyance
test:**
* **gi=24 (two cases) — a MISTAKE, fixed:** required-piece protection (`CleanupDiscardProtected`)
  dropped Throes of Chaos from the preference tier, silently overriding the ranking's own band-1
  rule (a retrace card is not LOST to a discard — the graveyard is where it stays castable from,
  and the land kept in its place pays the retrace cost). Fixed; `MTG_PROTECT_RETRACE=1` restores.
* **gi=444 — CLAIRVOYANCE, not worried:** the fan sheds the deck's ONLY visible Land's Edge on
  turn 2, right only because the fixed deck order is known to bring a replacement.
* **gi=229 — a second MISTAKE, fixed (user's question found it: "don't we potentially keep a
  Temple?"):** the keep set DOES buy a Temple (scry lands are diggers, dig_rank 3/4), but the dup
  rule banded EVERY copy of a duplicated name spare — `copies_seen` is symmetric, so it cannot
  tell "the second copy" from the first — and it preempts the keep-slot band, so Temple×2 shed
  BOTH and kept a surplus basic Island (T6 vs T5). Fixed: a kept copy is the first copy; only
  unkept duplicates band spare.

**Net result vs the old ground truth** (which embodied the whole-hand fan), with both ranking
fixes stacked: smoke th_d0 −0.0060, regression th_d3_s2002 −0.0040 (gi=273 T6→T5, gi=392 T7→T5),
th_d3_s3003 −0.0020, th_d5_s3003 −0.0033, everything else equal on avg (digest-only); every other
deck byte-identical; repro game 0.3 s flat d3–d8. **Every TH case is now net better or equal: the
ranking beats the searched fan it replaced**, at ~1/10,000th the cost — the fan's remaining
per-game wins are clairvoyant (gi=444). The method that got here is the loop to reuse: run the fan
as a DIAGNOSTIC referee on disagreement games, classify mistake-vs-clairvoyance, fix rankings.

The probe depth pin was subsequently REMOVED (see the status header): with the redesign in place
it bought nothing on any measured game, and it violated the one-engine-one-depth principle. If a
future fat-list provider re-creates the explosion, the remedy is the same one that worked here —
make that provider's ranking return the candidates it actually wants searched — not a fidelity
knob.

## Related

- `searched-cleanup-discard.md` — the probe whose candidate set is now provider-pruned.
- `post-breakpoint-search.md` — the searched continuation whose every-ply fan-out multiplied it.
- `breakpoint-width-deferred-waves-2026-07-29` — the wave phase (a constant here, kept).
- `depth-matrix-should-use-batch-pooling.md` — the harness half of the phase-C stall.

## MTG_CANON_SIMKEY adoption evidence (2026-08-14)

The canonical key went through the full verification pass on the Mirrorwing perf push:

- **Exposed a real latent bug first**: dragonstorm regressed under canon because BuildSimKey never
  folded `storage_counters`/`storage_hold_this_turn` — ordered keys had been keeping the colliding
  states apart by ACCIDENT of history-order. Fixed (`1593a926`, gated fold, non-storage decks
  byte-identical); dragonstorm recovered fully (one case byte-identical PASS).
- **Suite-wide quality (post-fix)**: net loss-penalized delta **-0.0328 over 55 cases** (better);
  every deck neutral-or-better except two thousandths-scale single-case moves. Mirrorwing paired
  3000-game A/B: **-0.0087 quality (t=-3.4) at 0.79x cost**; the gi=17 Class B monster: ~2.1x
  faster, same answer.
- **Reference-gate items all explained** (bisect method: replay the ref's choice prefix, halt both
  arms at each decision, diff position/plan lists): both ENUM-GAPs are BENIGN CANONICAL DEDUP —
  plan count shrinks but the cast-MULTISET sets are equal (dragonstorm T4: 78=78, zero lost;
  creature_giving T2: 13=13) — the vanished entries are order-permutations whose post-state
  survives under another ordering. Creature_giving's later "option content" change is a fetch
  (Windswept Heath) shuffling a differently-ordered line's library — sequence-derived, not a lost
  option. The Auras play-drift is one recorded game moving T5->T6, priced inside the suite net.
- **Remaining for the default flip**: the standing adoption gate only — GT rebaseline (digests move
  by construction) + re-saving the drifted reference via the viewer. Folds into the next joint
  chain rebaseline. Until then canon rides per-pipeline (Mirrorwing batches).

## Canon must-find hunt — state at 2026-08-14 end of session

REVERTED/BLOCKED (user invariant: unbounded budget must reproduce every old win; 6 games failed).
Three key holes fixed so far (`1593a926` storage, `433cda94` floating mana + attachment wiring —
the latter cured the Auras failure; float measurably improved ordered-mode mirrorwing too).
STILL FAILING under canon-on b0: antilife s6006 gi306 (5->6), mirrorwing gi191/223/267/363 (+1).

LOCALIZED REPRO (traced hunt, logs/mwprof/hunt_gi191_{on,off}.log): the first 35 search entries
match; the arms then split INSIDE a pre-game bottoming trial — same trial, same state, OFF commits
`T1 pass=2 done win=5`, ON commits `win=6`. A DEPTH-2 estimate divergence at unlimited budget =
the collision sits in the LEAF-ROLLOUT memo path (SimulateToEnd / leaf TT hit poisoned by a
canonical merge), not deep recursion. Next session: re-run that T1 trial search at d2 both modes
with MTG_FD_TRACE, diff the first divergent rollout, extract the two states sharing a canonical
key, name the missing field. Candidates not yet folded: stack CONTENTS (size only today),
pending-continuation/breakpoint state, cards_drawn gate breadth. No re-adoption until the full
must-find set passes; the set itself is now part of the canon gate.
