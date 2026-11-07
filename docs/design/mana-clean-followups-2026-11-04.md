# Mana-clean follow-ups queued 2026-11-04 (USER rulings)

Work the USER has ruled on during the 2026-11-04 mana-clean pass that is NOT yet assigned to an agent. Each entry keeps
the USER's words verbatim. Delete an entry when its work lands (or move it to the deck's own doc).

## 1. Memo for "can these spells be paid one after another?" (SubsetPayableSequential verdicts)
USER: *"Sure, we can give it a shot."* An engine-wide memo of the sequential-payability verdict; 67% of the checks
repeat an earlier "no". Must be an exact memo (same key = same answer); measure units/instructions.

## 2. EldraziDisplacerFlicker: conserve colourless in autonomous play
USER: *"Yes, EDF needs to be careful about colourless and should not generically do this for sure. I suppose it could
turn it on when there is nothing that needs it, but it might be easier to just keep it off. That isn't to say that we
shouldn't spend colourless when we are getting a stock of it, but that keeping 1 or even a few up is crucial in certain
boards. Particularly with Eldrazi Displacer itself. (and the wincons also need colourless)"*
Today `MTG_EDF_C_CONSERVE` runs in human (viewer) play only; autonomous EDF still pays generic with {C} first. Turning
the conserve rule on autonomously measured worse in two scenarios (`edf_blink_loop_cashes_deckout` T4->T5,
`edf_depleter_drain`): the autonomous loop collects mana by the same ranking and starved its pool of the {C} the
deck-out kill spends. Shape to build: generic does not take {C} first in EDF; keep at least one (a few, where the
board's {C} demands need it -- Displacer's {2}{C}, the {C} wincons) {C} source/unit available, and spend {C} freely
only beyond that stock. Root-cause the two scenarios first; held-out A/B (EDF std + 2HG). Note fix/c-wild already
exempts EDF from "floating {C} pays generic first" (`deck_spends_c_pip`).

## 3. Pirates: Malcolm and an Aether Vial'd Corsair Captain
USER: *"This is one of those cases where we could apply a 'if cost requires' rule if needed. The default order is the
ideal, though. That said, if the Captain is dropped off Aether Vial it no longer matters with respect to Malcolm's
order."* Seeds 30017240 / 30024789 T4: plan {Malcolm, Vial puts Corsair Captain}; only Vial-first pays Malcolm's {U}
(the Captain's ETB Treasure). The cast order (Malcolm 2nd, Captain 3rd) should not bind a Vial PUT (not a cast); the
put may go first. Only if that is not enough, an "if the cost requires" exception. Default order otherwise unchanged.

## 4. Deck-derived default colour to keep up (instead of a searched pre-draw payment option)
USER, rejecting a searched "how to pay before a draw" option: *"No, we shouldn't do that. If anything this is a case for
deck heuristics or perhaps some kind of analysis of the deck itself. That could be a good default way to handle this.
So if you have a lot of 1-drop blue spells you would prefer to keep up one blue by default rather than say red."*
Shape: from the decklist, derive which colour the deck's cheap spells (1-drops, cantrips, instants) most often need; when
a payment before a draw leaves a choice of which source to keep untapped and nothing in the line decides it, keep the
deck's most-demanded colour up. A provider-side ordering (allowed under NO-GREEDY), default computed per deck.

## 5. Matron tutor ranking cost (TutorCandidates ~25% of Goblins instructions)
USER: *"This is up to you, but probably when looking into fixing the issues with it."* Do it with the next TutorCandidates
change (after the Matron combat fix on wip/mc-matron lands).

## 6. Equip-route replay cap (KittyEquipment)
USER: *"I don't know what this is, but I suppose it can wait if you think it is low priority."* Low priority: a generous
node cap + warning on `equiproute::RunSearch` apply-mode replay (a replay of an already proven kill, so not a search
limit); needs a byte-identical held-out run.

## Settled, recorded here for reference
- FRESH-HOLD (Mirrorwing Gold Rush Treasure) stays disclosed, not failed. USER: *"This is the way it is because it
  shouldn't cut off anything significant. If you think keeping this rule has risks then it should be proven with tests.
  I can't think of an effective case where I would want this in the deck and I think it is a good rule, but it is
  difficult to imagine every scenario."*
- 0cf0348c4 (Goblins plan-added fodder): wait for the Matron fix. USER: *"I would rather not enable something failing
  the bar for the most part. That isn't to say that there are not correctness fixes that require such, but usually they
  come with an actual win to help offset the losses."*
- CAPPED retry + node cap: USER *"Sure. We may even want a higher threshold depending."* (being measured on
  wip/oracle-nodecap).

## 7. LINE_REPAY (payment redo): USER direction 2026-11-06
Parked on `wip/mc-dragons` 06d079365 (EDF +11 / EDF2HG +3 on 5k/1.5k games; 4 slower games recover only under
`MTG_NC_SEARCH=1`; drafted fix for the land-Aura host hold perturbing the redo's find digest, not built). USER:
*"I can't say even the clairvoyant version would that disastrous, given that everything else is clairvoyant.
Naturally, I prefer the other version, though. The main idea, though. is that the user must be able to come up with a
plan and execute it based on cards in hand allocating mana as necessary and the search must be able to do the same.
The clairvoyant idea of what to hold up is something we could add a heuristic for, but should try not to touch
otherwise if possible."*

How this applies:
- The redo stays the non-clairvoyant kind (re-pay only against cards known at that payment), per the 11-03 ruling.
- Its purpose is that principle: a plan made from the cards in hand must be payable by the search exactly as a player
  would allocate it. A search/executor mismatch (the host-hold defect: the search keeps mana for Eldrazi Displacer,
  the executor spends it on the Aura) is a correctness defect to fix, not a policy question.
- "What to hold up" against unseen cards (Dragonstorm {B} for Apex, Mirrorwing H1/H2) belongs in provider heuristics,
  not in the redo or the search.
- The price (4 vs 12 probes a unit, pricing pass 1) is budget accounting, inert at unlimited budget. Default when the
  work resumes: 12 and price pass 1, measured; still open for the USER to override.

### 7a. USER ruling 2026-11-07 on the price (supersedes "default 12 + price pass 1")
*"We should charge the redo fairly and also look to minimize how often it happens. For certain, we should only be
doing cases that have potential to fix the colours, not those that require more total mana."*
- Fair charge: the price follows the measured cost of a probe against a unit (re-measure; not the dear end, 4).
- Minimise how often it runs: the redo is ADMITTED only when re-allocating could fix it -- the line is short of a
  COLOUR, not of total mana. A line whose remaining bill exceeds the most mana the board could make in total
  (every source at its best yield, floating mana included) cannot be fixed by re-paying and must not start a redo.
  This is a sound identity prune (no re-payment changes the total), so it is default ON.
- FOUND 2026-11-07: `wip/mc-dragons` already has both halves. LineRepayTry refuses a bill the board cannot cover by
  AMOUNT (`PaymentManaCovers`, before any search; counter `amount_short`) and, per depth, a chain whose JOINT bill
  (every re-made payment plus the failing one) does not pay on the oldest board (`ch.JointPays`, `joint_short`) --
  the second is the colour test: only a re-allocation that can exist is searched. Next: measure the funnel
  (recorded / tried / amount_short / joint_short / fired, and pass-2 probes) plus probe-vs-unit cost on EDF, Dragons,
  Angels (300 games each, seeds 88100000+, `build/scratch/mcdragons/lrprice/`, queued behind the pass driver), then
  set the price to the measured ratio and look for what still reaches the priced passes without re-making anything.

## 8. USER rulings 2026-11-06 on Q23 / Q25 / Q29 (open-questions file, 2026-11-04)
- **Q23 Dragonstorm {B} hold before Apex: NOT adopted as built; root-cause gi37 (2HG, seed 1920001+37).** The hold
  is added to the line's named demand, so a search payability walk may see the Apex line one mana short and
  undervalue it (hypothesis, unverified). By the 11-06 principle (section 7) the hold may only ORDER spending; it must
  never make a payable plan look unpayable to the search. Branch `worktree-agent-afe476c961652c9b8` (9eaae0e16,
  76381fad5; drop TEMP 212197e8d).
- **Q25a Mirrorwing Std +1 (g662, seed 73000662).** USER: *"It might be good to actually look at the game and see how
  the clairvoyance is working if that is the case just to be certain our decision is correct."* So before accepting,
  replay g662 base vs new (play settings, then `MTG_NC_SEARCH=1`) with game logs, and show where the base's T4 win
  depends on the draw order. Branch `wip/mc-mirrorwing` (fa7990254, 3d9a6d692, f6572c7a7).
- **Q25b H1/H2 hedges: build both.** USER: *"Those would be fine and I that should be the order anyway, particularly on
  Mirrorwing."* H1: on an attack turn with a copy creature out or in the plan, pay cantrips from lands (even if mana
  floats) before tapping a creature that could attack. H2: while a draw is still to come in the line, a Treasure pays
  only pips no untapped land can. Payer-order changes: held-out A/B on the decks that move.
- **Q29 known cards across reshuffles.** USER: *"I don't know how the training labels work entirely, but that makes
  some sense. We should also fix the non-clairvoyant search if it doesn't handle this."* So: port `ShuffleUnseen`
  (cc3b47aea, `wip/niv-nc-known-top`) onto main's `m_known_top`; make the non-clairvoyant search (`BlindUnknownOrder`
  / `MTG_NC_SEARCH`) keep known top/bottom cards (verify on Niv gi887, seed 1650001); switch `EmitEvalRows` training
  labels to it. Blind leaf and keep trainer unchanged.

## Galadriel: the oracle-on arm exhausts memory (found 2026-11-04 16:30Z, deferred)

The pass run `pass1-galadriel-r2` (src `aa9f15b9b77f`) hit the 43 GB RSS cap about two minutes into the full oracle-ON
batch (32 threads) and aborted with exit 137 after 564 games. Those games logged 2,966 UNMODELED "payment enumeration
capped" flags, nearly all Crackle with Power with a large X on boards of 10+ lands (e.g. seed 30000018 gi 18 T4, seed
30000022 gi 22 T4). The oracle-OFF arm is unaffected. The same run had started at 12:36Z alongside other jobs and is the
likely cause of the container OOM at ~12:50Z. Games in flight at the abort: gi 1457, 1827, 2157, 2179, 2291, 2417, 2445.
Galadriel is NOT requeued: root-cause the oracle's memory on X-spell payment enumeration first (per-game RSS with
`MTG_MANA_ORACLE=2`, one thread), then rerun the deck alone.

## 9. Analyzer stalls: the oracle's per-plan replays (diagnosed 2026-11-06, NOT fixed yet)
USER 2026-11-06: *"Yes, I would like to sort out the stall cases. Whatever the case we don't want to get indefinitely
stuck on those. If it is the engine's search being slow that might be difficult to fix, but I suspect it would be easier
if it is just the analyzer, since I don't believe it has been optimized much."*

Evidence: LandfallElementals pass1-landfallelementals-r3 (src 7bd2b9ac28ff), on arm 1663/1667 chunks after 2 h, with 4
games at ~2 h (gi 8496 and 9503 also stalled 11-05; 11201, 11679). `sudo gdb` of the batch (stacks in
`build/scratch/mana_clean/postpass/samples/lfe-live.bt`): EVERY busy thread is in
`manaoracle::AuditDecision > AuditPlan > Realize > TurnSolver::ApplyPlan > BpUnbranchedCanon > BpDeriveContinuationList >
EnumeratePlans(WithLand)`. A 45 s `perf` profile: the self time is all plan enumeration and copying (BuildSimKeyBase,
AppendSubdecisionAxes, Plan copy/dtor, EnumeratePlans, GameState copy) plus `ParetoKeep` ~2%. So the play search is not
stuck; the ANALYZER is, and its cost is that each oracle "node" is a full `ApplyPlan` from the decision board, which on a
breakpoint deck re-derives the continuation menu every time. Bounds today: `MTG_MANA_ORACLE_NODES` 2000 applies per
plan, then the capped retry (`_RETRY`), times every plan on the menu (`MaxPlans`) plus up to 400 ENUM-GAP gate checks
(`MTG_MANA_ORACLE_GATE_MAX`). Nothing bounds a DECISION or a GAME, so one decision can take hours.

CONFIRMED for every deferred deck 12:38Z: with MTG_MANA_ORACLE=0 the EDF gi 19/61, Alchemist gi 29/59, Niv gi 47 games finish in 7 s total (`postpass/stall.o0.out`); they stall only with the analyzer on (all stall again after the 11-06
time fix). The post-pass chain (`postpass/chain.sh`, stall.json, arms ORACLE=0 then 2) plus `postpass/sampler.sh`
(gdb + perf every ~5 min into `postpass/samples/a<arm>-*`) confirms each: an OFF-arm game that finishes fast = analyzer.

Fix, in order (analyzer only, play untouched, so no NO-GREEDY or GT question):
1. Make an apply cheap: within one Realize all scripts apply the SAME plan from the SAME root, so the continuation
   menus `BpDeriveContinuationList` builds repeat across scripts whenever the paid board is identical. Memo them per
   Realize (key = the sim key of the board at the breakpoint), exact (same key = same menu). Measure applies/s and
   the output byte-identical (`MTG_ORACLE_RESUME_CHECK`-style self-check).
2. Never indefinitely stuck: a per-DECISION apply budget (env, generous, e.g. 50k applies) after which the remaining
   plans are reported as a new CAPPED-DECISION census bucket (partial, never a verdict), counted in the report so a
   deck cannot pass clean on capped decisions. This is the measurement's own bound, inert below the cap.
3. Re-run the deferred decks (eldrazidisplacerflicker, accomplishedalchemist, nivmizzetcombo, landfallelementals) and
   check the stall games finish.

## 10. USER ruling 2026-11-07: Hinata regression d5 s3003 gi27 on fix/c-wild
*"For 2 I think it is fine to move on if we can show it is recoverable at any depth + budget."* Shown: the c-wild
binary (gcf3) at d5 b300 WINS T8 (= old turn; `build/scratch/cwild/regslow/g27.out`, digest 9c9019ccf4c39588); d5 b0
loses on BOTH arms. So gi27 is dispositioned recoverable; c-wild is clear to land (rebase onto origin, rebuild, smoke,
accept GT through the normal flow).

