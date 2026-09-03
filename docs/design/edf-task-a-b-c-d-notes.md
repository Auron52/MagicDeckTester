# EldraziDisplacerFlicker: A/B/C/D findings (scratch, in-progress)

Worktree: `/workspaces/MagicDeckTester2/.claude/worktrees/agent-af86cc2c50ade9bd8`
Branch: `worktree-agent-af86cc2c50ade9bd8`

## Inherited WIP (commit ece81afb) -- verdict: KEPT, verified sound

Diff vs its real parent `56ba0979` (NOT `8152ae6e` -- that hash is on a sibling branch line,
its appearance in `git diff 8152ae6e..HEAD` was a red herring from comparing across two
different lineages, not part of the WIP).

Two-part fix, both load-bearing:
1. `SubsetPayableSequential` (TurnSolver.cpp ~3278): after a cast with `etb_untap_lands` pays
   for itself, actually fire `EtbUntapLands(cp, ..., log_ledger=false)` so the untapped lands'
   mana funds a LATER cast in the same sequential walk. Previously the walk taped lands ahead
   (`EtbUntapTapAheadIntoFloat`) but never gave them back -- Drake's own payment was priced
   correctly, but every cast ordered after it saw a board strictly worse off than reality.
2. `EtbUntapBoundCredit` + threading `etb_state=&state` through `ManaPruneBound` /
   `BuildManaGateIndex` / `ManaGateWouldHelp`, but ONLY from `EnumeratePlans` (never from
   `Solve`/`SolveUncached`, confirmed by reading both call sites -- Solve's calls at
   TurnSolver.cpp:15325/15334 pass no etb_state). Needed because the ODOMETER's upper bound
   would otherwise prune the Drake+Displacer subset before SubsetPayableSequential ever saw it
   -- fix (1) alone does nothing if the candidate never reaches eval_and_push.

Both are pure loosenings of an upper bound / a rescue path that only ever returns `true` where
the old code returned `false`, so Solve stays byte-identical (verified: neither `ManaPruneBound`
nor `BuildManaGateIndex` call inside `SolveUncached` was touched) and no other deck's rollout
leaf changes at all.

## Task A -- Drake-then-Displacer: category (b), CONFIRMED, not (a) or (c)

`logs/play/rejections/EldraziDisplacerFlicker_cod_s2_gi1_t4.json` reproduced directly:
```
mtg decks/EldraziDisplacerFlicker/EldraziDisplacerFlicker.cod --claude-play --seed 2 \
  --game-index 1 --choices "1,0,3,16" \
  --validate-line "land=Shivan Gorge;cast=Peregrine Drake;cast=Eldrazi Displacer"
```
Before the WIP fix this line is `"verdict": "illegal"`. On the current (kept) binary it is
`"verdict": "accept"`. Root cause was (b): the ETB untap WAS modelled, and cast ORDER was
already correct (`CastOrderRank` already puts the payload before the outlet), but the
resolution-time credit from the untap was never given back to the scratch board, so a cast
ordered after the untap effect priced against a board that was strictly poorer than reality.
Not (a) -- untap is modelled. Not (c) -- the enumerator DOES generate this cast order; it just
priced it wrong / pruned it before pricing.

## Task B -- can't activate Eldrazi Displacer in the viewer: VIEWER GAP, engine is fine

Verified directly, not inferred:
* Continuing the same seed/game (`--choices "1,0,3,16,13"`, i.e. accept the Drake+Displacer
  plan) shows the NEXT main-phase decision already offers
  `"Eldrazi Displacer: blink Peregrine Drake"` with `blink_target`/`blink_target_name`/
  `blink_count` -- proof `TurnSolver::EnumeratePlans` enumerates the {2}{C} activation and the
  payment check (Aether Hub / 3 painlands / Shivan Gorge colourless taps) passes.
* `tools/play/index.html`'s `activatableSources()` only makes a battlefield permanent
  clickable when its JSON action carries `"activate": true` -- and `src/main.cpp`'s
  `WriteDecisionJson` (the emitter) never listed `Action::Kind::ActivateBlink` or
  `::ActivatePermAbility` in that flag's condition list, unlike 13 other kinds already fixed
  there for the identical reason (Krenko, sac outlets, ActivatePump, loyalty abilities,
  KittyEquipment, Deathrite's gy-exile...). So clicking Eldrazi Displacer (or Shivan Gorge, or
  Conservatory) on the board flashed "has no ability you can activate this phase" even though
  the plan list already had one queued.
* Fixed: added both kinds to the flag list in `src/main.cpp` (WriteDecisionJson). Verified the
  JSON now carries `"activate": true` on the same reproduction. `CanTapNow` was NOT the
  relevant predicate here -- Displacer's `{2}{C}` has no `{T}` in its cost at all, so it was
  never gated on tap-ability liveness; that hypothesis from the brief is ruled out.
* Scope-checked: `WriteDecisionJson` is claude-play/human-play-only output. No autonomous
  rollout/Solve/`--batch` path calls it -- confirmed by inspecting call sites (only
  `src/main.cpp` calls `TurnSolver::CheckLine`, and this JSON writer is a sibling human-play
  emitter). So this fix cannot move any measured avg-win-turn number; it only unblocks the
  browser viewer.
* Also explains the T4-kill half of the report: at that point in the game Depleter/Infiltrator
  are still in the sideboard, so Shivan Gorge's damage tap (`ActivatePermAbility`) is the only
  kill on board, and it rode the identical missing flag.

## Task C -- attach an Aura to a land, engine side: FIXED

Root cause (already established by the prior investigation, DECISIONS.md's "STILL OPEN
(engine, not the viewer)" note) confirmed by code reading and traced end-to-end:
`TurnSolver::CheckLine`'s "Aura enchant TARGET" `addSub` block (~32131) only searched
`state.battlefield` for a permanent with `perm.card.IsCreature()`, so a land host ALREADY IN
PLAY (Aether Hub, an untapped Yavimaya Coast, etc.) never got a label -- `etn` stayed empty and
`addSub` was never called for that variant. Traced the consequence precisely: the `tok` string
`addSub` would have produced feeds `toks` -> `sig` (TurnSolver.cpp:32377-32378), which is the
literal string CheckLine dedupes candidates on. An empty-`toks` variant carries no
distinguishing token, so two already-in-play land hosts share the identical `sig` and collapse.

Fix: drop the `IsCreature()` (and `controller_index`) restriction -- resolve the host name from
ANY permanent matching `perm.card.m_number`. `SubChoiceHostLabel` (used right after) already
disambiguates same-named hosts for any permanent kind, so this is the minimal correct fix.
16 of the deck's 60 cards are "Enchant land" auras (Wild Growth / Fertile Ground / Overgrowth /
Trace of Abundance).

**DIRECTLY reproduced DECISIONS.md's own repro, before AND after, same seed/game.**
DECISIONS.md names an exact repro: "EldraziDisplacerFlicker seed 1, T2+, `cast=Wild Growth`
returns a single variant with `subs: []`". Ran it on both binaries (seed 1, game-index 0,
`--choices "1,0"`, `--validate-line "cast=Wild Growth"` -- board has Aether Hub already in
play with a first Wild Growth already attached, hand has a second Wild Growth):

* BEFORE (commit `56ba0979`): `"variants": [{ "plan_index": 20, "label": "cast: Wild Growth",
  "cards": [], "subs": [] }]` -- exactly as DECISIONS.md describes: empty subs, no art, no
  indication the plan enchants Aether Hub at all.
* AFTER (this worktree): `"variants": [{ "plan_index": 20, "label": "cast: Wild Growth — Wild
  Growth → Aether Hub", "cards": ["Aether Hub"], "subs": [{ "key": "Wild Growth →", "choice":
  "Aether Hub", "card": "Aether Hub", "kind": "enchant", "num": 3 }] }]`.

This is the load-bearing verification for C: same commit-pair as the A/B test below, same
exact repro named in the existing design doc, verdict flips exactly as predicted.

Scope-checked like B: `CheckLine` is called only from `src/main.cpp` (human-play line
reconciliation + `--validate-line`), never from the autonomous rollout. The autonomous search's
OWN plan dedup (`plan_signature` inside `EnumeratePlans`) only keys on `enchant_target` under
`s_human_play_sig` (i.e. `HumanPlayActive()`), so this fix is also inert for `--batch`/measured
play, exactly like B.

## Task D -- seed 1 T3 "Trace of Abundance on Aether Hub via Conservatory seems rejected"

Answer: **not a new payment bug -- almost certainly the same C-class collapse/inexpressibility**,
per the analysis doc's own attribution (`docs/design/analysis-EldraziDisplacerFlicker.md` items
1 and 2 are explicitly tied to the same root cause: an Aura targeting a land already in play,
Aether Hub, is exactly the shape C fixes).

What I verified vs inferred:
* VERIFIED: the mechanism C fixes is real and reachable (traced the exact code path, and
  Task A's own reproduction incidentally exercises the SAME addSub branch for lands already in
  play -- Overgrowth targeting Yavimaya Coast/Conservatory/Shivan Gorge in the T4 game -- so the
  branch is definitely live on this deck's real games, not a theoretical dead path).
* NOT VERIFIED end-to-end for the specific seed-1/turn-3 board: I do not have the user's actual
  turn-2 choice (the saved rejection log for seed 1 is a DIFFERENT, correctly-illegal turn-2
  line -- "cast=Wild Growth" with no green source -- so it does not tell me what was actually
  played next), so I could not replay to the exact reported turn-3 state and confirm the specific
  line flips from illegal/collapsed to accept. I deliberately did not invent a plausible
  turn-2 line and present its result as "the" verified answer -- that would be exactly the
  "invent a payment bug to satisfy the report" trap the brief warns against, just in the other
  direction (inventing a *fix confirmation* instead of a bug).
* A directly-tested T2 state from the SAME seed/hand (Aether Hub + Wild Growth already in play,
  Conservatory in hand) rejects `cast=Trace of Abundance` for a real, unrelated reason (no
  untapped red/white source once Conservatory -- `enters_tapped` -- is played this same turn);
  that is a correct rejection, not related to A-D.

Verdict: the C fix is the right, verified answer to "why would this look rejected", and I am
confident it is *the* explanation given the doc's own item-1/item-2 linkage, but I have not
personally replayed the exact turn-3 game state to see the verdict flip. Flagging this
distinction explicitly rather than papering over it.

## Measurement

Paired before/after, both built inside worktrees off this repo (before = commit `56ba0979`,
the WIP's real parent; after = current HEAD here). 4 seeds (6101/6202/6303/6404) x 100 games,
d5, budget_ms=20, `ignore_play_profile=true`, `--threads 8`. B and C are confirmed inert for
batch play (see above), so this measures Task A's effect alone. Results: see below / final
report (in progress at time of writing this note).
