# gate1127 triage: handoff (2026-11-28 11:55 UTC)

## Done
- `logs/gate1127-triage/ranking.md` holds three things:
  - 15 ranked engine-miss clusters and 6 oracle-side items for the mana-clean gate `claude-queue-20261127a`
    (src `80cb93166cfb`);
  - a fix brief per cluster, with file:line citations at HEAD and repro seeds;
  - the open USER questions.
- Commits on `wip/gate1127-triage`: 12c0c14ff (first version), then this commit.
- `parse.py` re-derives the events from the gate's raw chunks. It writes `build/scratch/gate1127_recs.pkl`: every owned
  ISSUE line, counted only from the attempt that finished its job. `events.py`, `clus.py` and `show.py` cluster the
  events and print samples. The sample flags are in `samples/`.

## Not done
1. **Every impact grade is unmeasured.** The lane was full for the whole session (ds-apply5 `gate_base`, 11 h on 2
   slots; evrun on 2 slots; three bs-oracle jobs queued ahead), so no repro ran.
2. **The repro job is still queued:** box_lane PID 2805219 runs
   `/workspaces/MagicDeckTester/build/scratch/gate1127-triage/repro1.sh`, 10 single games (list in the ranking.md
   appendix). It writes `<tag>.{out,err}`, `<tag>.logs/` (game logs), `progress.txt` and `DONE1` in that directory.
   Nobody will be woken when it finishes. Read it then, or `kill 2805219` if it is no longer wanted. It is this
   triage's own experiment, so killing it is fine.
3. When it lands, fold in, in this order:
   - (a) `mill-surg.gate.err`: a `[dbgmulti] ... why=` line names the Mill gate (cluster 12). No line means the odometer
     bound cut it, or the covering entry is a continuation, which makes it oracle-side.
   - (b) `th-*.post`: check that the TH flags are gone on the post-gate binary (cluster 14).
   - (c) `bst-enum.gate` / `cg-crop2.gate` / `ds-enum.gate`: the gate names, to confirm the code-read mechanisms of
     clusters 2, 6 and 13.
   - (d) `wf-ponder.post`, `dr-bolt.post`, `ang-drop.post`: whether clusters 4, 7 and 11 still flag at HEAD.
   - (e) `bst-incub.gate.logs`: what the engine played on the Incubator turn, and the game's win turn.
4. **To measure play impact (owed), per cluster:** pair the flagged games (seed lists come from `parse.py`'s pickle)
   with and without a prototype fix, and confirm slower/faster at d8 b0.
   - Cheapest prototype: Beasts cluster 1, an order-rescue domain for `chooses_creature_type` reducers
     (TurnSolver.cpp ~8829-8880).
   - Mill: `MTG_UNPRUNE=surgicalcost` on the 268 flagged games is a ready-made lever.
5. **Re-gate at HEAD before fixing:** Treasure Hunt, Dragonstorm non-Karrthus, Worldfire and Shadowborn. Fixes landed
   after the gate's commit (see ranking.md) target those decks' clusters.

## Briefing constraints (verbatim from the parent)
- "Do NOT take /workspaces/MagicDeckTester/build/scratch/claude_box.lock or run any multi-thread batch: the big lane
  belongs to the Dragonstorm agent."
- "SKIP these: Blood Sun ..., Dragonstorm's Karrthus need-later cluster (DS round 5 agent, MTG_VISIBLE_CLOSER_KEEP),
  and Treasure Hunt s30101394."
- "Never push phase-1-2-deck-analyzer."

## Open USER questions
These are in ranking.md section 5:
- the Stuffy Doll Act/Chain Reaction fallback;
- the 1 Drop Burn Devil-before-Guide fallback;
- Beasts Incubator: the order rescue vs a cast-order rank change.
