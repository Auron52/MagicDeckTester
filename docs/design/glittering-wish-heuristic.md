# Glittering Wish candidate heuristic (Bruna) -- status 2026-10-06

Branch `wish-heuristic` (base origin `e1171a1d`). **NOT landed on `phase-1-2-deck-analyzer`**: the
final rule's proof is incomplete (see "Owed") and no suite run has been done on it. Session closed
on a hard deadline. Everything below can be picked up from this doc alone.

## What is implemented (committed on the branch)

* `BrunaProvider::TutorCandidates` (src/ai/DecisionProviders.cpp, "the GLITTERING WISH candidate
  rule"): Glittering Wish's 11-name multicolored sideboard narrows to a candidate set, and the search
  chooses among those candidates (TutorSearchWidth 32 still covers them all). Roles come from PARAMS:
  1. **Bruna** (`attack_gather_auras`): only if no copy is in our hand or on our battlefield (legend rule).
  2. **Primary Aura**: the payload Aura (not cheap, MV > 3) with the highest resulting power on its
     best host (our creatures + creature cards in hand). Almost Perfect sets base power 9, so it adds
     9 minus the host's base. Ties go to sideboard order, so with Bruna as the only host Umbra and
     Almost Perfect tie at +4 and Umbra is listed first.
  3. **Bodies** (non-mana-dork creatures): only when we have no body (no non-dork creature on the
     battlefield and none with MV <= 3 in hand). Offered: the cheapest one (Vexing Shusher) and the
     hardest-hitting one with MV <= 3 (Linvala).
  4. **Troyan**: only when mana is short. That means next turn's supply (lands, rocks and unrestricted
     dorks at their yield, +1 if a land is in hand; creature-only mana counts toward creatures) is
     below the largest MV >= 5 we mean to cast (the hand, plus the 6-drop the wish could fetch).
  5. **Cheap Aura** (MV <= 3, at most one): only as a second Aura beside a primary, with no
     cheat-into-play path (Bruna on board or castable next turn; Arcanum Wings on board, or in hand
     with a creature and mana for cast + swap), and only with a body on board.
  * Order (front = base plan / rollout / d0 pick): Bruna, primary Aura, hardest-hitting body,
    cheapest body, Troyan, cheap Aura. An empty set falls back to the full legal list.
  * Excluded: Detention Sphere, Auroral Procession, Reborn Hope, and any non-top Aura.
* **Control arm** `MTG_WISH_FULL_WIDTH=1` (heurarm slot `WISH_FULL_WIDTH`, default OFF) returns
  GenericProvider's list. Verified byte-identical to e1171a1d: the control arm reproduced all four
  Bruna smoke GT digests (d0/d3/d5/2HG).
* Executor, rollout, d0 and search all read this one hook. `MTG_UNPRUNED` and human play keep all 11
  names.
* **Viewer mark:** `DecisionProvider::TutorMarksSuggested` (Bruna: Glittering Wish). In
  `AskHumanTutorPick`, the names in the narrowed set are passed to the tutor chooser as `suggested`
  indices. `WriteTutorDecisionJson` emits `"suggested": true` on those candidates only, so every
  other tutor's payload is byte-identical. `index.html` `tutorPanelHtml` shows a green "suggested"
  badge and ring, and the tile order is unchanged. Verified with claude-play seed 1: 11 offered,
  3 marked. Documented in tools/play/DECISIONS.md.
* **Instruments:**
  * `MTG_TRACE=wishproof`: at a REAL resolution, prints the rule's set in either arm plus the board
    and hand. Pair it with `MTG_TUTOR_CHOSEN_RANK=1` (`chose=`) to test whether a control-arm fetch
    was inside the set.
  * `MTG_TRACE=wishcands`: every evaluation.
* **Unit tests** (test/unit/test_bruna_sideboard.cpp, 7 "Glittering Wish rule" cases): never a
  duplicate Bruna; Almost Perfect, then Umbra; a cheap Aura never as the sole pick and only without
  a cheat path; Troyan only when mana is short (including the Conscription {8} case); bodies only
  without a body; other tutors unchanged; the control arm gives the full 11. `mtg-test` 462/462
  passed on the pre-final binary, and the 7 wish cases pass on the final one.

## Proof history (pooled batches, both arms, Bruna play settings; logs under logs/wish/, gitignored)

Cells: d5 b20 s10.1M x400, v5 s10.5M x400, d3 b10 s10.2M x200, 2HG d3 s10.6M x200, d0 s10.3M x1000,
held-out f5 s11.5M x400. "Worse" means the control (full width) won sooner. Loss counts as 9.

| round | rule | d5 | v5 | f5 (held-out) | d3 | 2HG |
|---|---|---|---|---|---|---|
| 1 | spec roles only (no body) | 7 better / 4 worse | 3 / 10 | -- | 0 / 0 | 1 / 0 |
| 2 | + cheapest body (Shusher) | 6 / 1 | 2 / 5 | 2 / 6 | 0 / 0 | 1 / 0 |
| 3 (final code) | + hardest-hitting body (Linvala) | 6 / 2 | 2 / 3 | 1 / 3 | 1 / 1 | 1 / 0 |

* **Round 1 diagnosis.** 9 of the 14 control wins fetched Vexing Shusher (7) or Linvala (2). In each
  one the board was empty or held only dorks, and the hand held Colossification, Mythic Proportions
  or Arcanum Wings. A host body was missing from the rule, so the BODY role was added.
* **Round 2 diagnosis.** Held-out f5 gi264 and gi371 never recover, even at d8 b0. The control
  fetched Linvala, and her third point of damage is the kill. In gi371 (same kept hand),
  Bruna + Conscription + Linvala deals exactly 18 against 17 life; Shusher's 2 power falls one short.
  So the hardest-hitting body was added.
* **Round 3 = the final code.** The d3 cell is byte-identical to the swept variant (digest
  `353c1538231e338f`). Both f5 gi264 and gi371 are fixed. The remaining control wins are d5 gi63 and
  gi101, v5 gi81, gi131 and gi269, f5 gi255, gi317 and gi393, and d3 gi117. Under the round-2 rule
  each of those except d3 gi117 recovered in the heuristic arm:
  * at two-stage stage 1 (d=<control win turn> b100): d5 gi101, v5 gi81, gi131, gi269, f5 gi255,
    gi393;
  * at stage 2 (d8 b0): f5 gi317.
  * d5 gi63 was classified as search allocation: the control's T3 wish fetched a dead duplicate
    Bruna, and its line is inside the set with any fetch.
  * The kept hand differed in d5 gi101 and f5 gi186/255/264/317/393. Bottoming rollouts read the
    wish rule, so these are physically different games.
  * **Owed:** re-run the recovery checks on the FINAL binary, and root-cause d3 gi117, which is new
    in round 3.
* **Cuts that were measured and REJECTED** (per-variant sweeps; temporary selectors, now deleted).
  These leave the rule at up to five names, against a 1-3 target:
  * drop Troyan when a body is needed: d5 +10, d3 +3, 2HG +10 turns;
  * drop the body when Troyan is offered: v5 +4;
  * drop Almost Perfect when no body, no cheat path and Bruna is offered: worse on all four cells;
  * drop the cheap Aura when Troyan is offered: v5 +1, plus 2 new worse games;
  * Shusher only, or Linvala only: covered above.
  The dominant early state (a T2/T3 wish, no body, no cheat path, short mana) is genuinely five-way:
  Bruna, Almost Perfect, Linvala, Shusher, Troyan.
* **Candidate-count distribution** (round 2, committed wishes incl. d0): 1: 9.5%, 2: 16.4%, 3: 30.2%,
  4: 43.9%. With the second body, the 4-sets of the early state become 5. **Owed:** re-measure on
  the final binary.

## Cost (round-2 rule, pooled, same batch; units are deterministic, ms are the box's)

| cell | heur / control units | heur / control ms | old pre-sideboard list (control behaviour) |
|---|---|---|---|
| d5 s10.1M | 67.5M / 94.3M = **0.72x** | 0.75x | 68.8M units (heur new list = 0.98x of it) |
| f5 s11.5M | 63.0M / 81.2M = 0.78x | 0.74x | -- |
| d3 | 17.1M / 28.7M = **0.60x** | 0.60x | 17.1M (1.00x) |
| 2HG d3 | 20.6M / 36.2M = 0.57x | 0.61x | -- |

The final rule (round 3) costs slightly more: d5 70.2M (0.74x), d3 17.9M (0.62x), 2HG 21.9M (0.60x).
Average turn: d5 heur 5.1050 / control 5.1150; d3 5.0300 / 5.0300; 2HG 5.5050 / 5.5100; f5 4.9725 /
4.9675. Old list: d5 5.1675, d3 5.0700. The d0 front-pick change on 1000 games: 28 better, 6 worse,
-22 turns. (d0 has no branched control; its worse games are greedy-line differences.)

## Next steps (in order)

1. On the branch binary, run one pooled batch with BOTH arms on every cell above plus a NEW held-out
   seed set (e.g. 12.5M x400). Run two-stage recovery on every control win (`logs/wish/recover.py`
   pattern: `--seed base+gi --game-index gi`, stage 1 `--depth <win turn> --budget-ms 100
   --ignore-play-profile`, stage 2 `--depth 8 --budget-ms 0`). Root-cause d3 gi117.
2. Re-measure the candidate-count distribution (`MTG_TUTOR_CHOSEN_RANK=1`, parse
   `src=Glittering Wish ... rank=r/N`).
3. If clean: rebase onto origin, `./build.sh`, run smoke + regression, and give a verdict on every
   difference. Expect bruna/bruna2hg to move (d0 included); everything else should be
   byte-identical, because the hook is BrunaProvider-only and the viewer mark is human-play-only.
   Then accept, run `check_gt_logs.py`, append the ledger section "Glittering Wish heuristic
   2026-10-06" to analysis-Bruna.md, push, watch CI.

## Open question for the USER (default taken: keep five)

The spec asked for "1-3 candidates almost always". The proof shows that every cut of the early
five-way state (Bruna / Almost Perfect / Linvala / Vexing Shusher / Troyan) loses games to the
full-width control. Keeping up to five still costs only 0.6-0.75x units of full width, and the
new-list cost comes back to the old list's level. Default taken: correctness over size, keep five.

## USER spec 2026-10-07 (recorded verbatim; the work below is OWED)

**Glittering Wish refinements** (round 5 implements most of them; see commits 375411d2..f4881764):
* "I would leave out Vexing Shusher. Others should be skippable based on the current conditions."
* "Troyan is basically good if you need acceleration and not worth it otherwise. An aura may not be
  necessary if we have good ones (and enough for lethal) in hand."
* "Linvala should lose in other situations... if I have the choice to cast bruna or Linvala next turn
  then we should definitely cast Bruna."
* "We might also have to have Arcanum Wings in hand for Linvala to be worth it?" (a hypothesis, to be settled
  by the games).

**Open the Armory candidate rule (NOT started).** Same hook (`BrunaProvider::TutorCandidates`), same
proof against a full-width control:
* "Open the Armory should only have Colossification, Arcanum Wings, Wild Growth and maybe Eldrazi
  Conscription as targets. Though, to be honest, I don't think we even need the Conscription when
  Goldfishing. Wild Growth is not needed unless we need mana or acceleration and Arcanum Wings is only
  useful if we don't already have one. Technically Colossification is not needed if we already have one
  either." / "Actually, we also need to consider Lightning Greaves." / "So, up to 5 options, but most
  can be dropped based on the circumstances." / "Actually, I guess 4 is enough really." / "And then we
  could heuristically eliminate more of them since you rarely have need of all of them."
* So there are at most 4 candidates, each conditional:
  * Colossification and Arcanum Wings, each only when we don't already have one.
  * Wild Growth, only when we need mana or acceleration (share the Troyan acceleration test).
  * Lightning Greaves, with its condition derived from the games (e.g. no Greaves already, and an
    attacker needs haste).
* Eldrazi Conscription is OUT. Mythic Proportions and Prodigious Growth are not on the user's list
  either, so they are out too. Measure what each exclusion costs and REPORT it; the user decides.
* Then narrow further heuristically. The user: "you rarely have need of all of them".

**Also asked:** "What other major branch sources do we have?" This needs a fresh census on the
current build. The last census (2026-10-05, pre-sideboard) found no Bruna-specific site; 68.7% of the
units were mulligan-bottoming playouts.
