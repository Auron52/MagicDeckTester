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

## Galadriel: the oracle-on arm exhausts memory (found 2026-11-04 16:30Z, deferred)

The pass run `pass1-galadriel-r2` (src `aa9f15b9b77f`) hit the 43 GB RSS cap about two minutes into the full oracle-ON
batch (32 threads) and aborted with exit 137 after 564 games. Those games logged 2,966 UNMODELED "payment enumeration
capped" flags, nearly all Crackle with Power with a large X on boards of 10+ lands (e.g. seed 30000018 gi 18 T4, seed
30000022 gi 22 T4). The oracle-OFF arm is unaffected. The same run had started at 12:36Z alongside other jobs and is the
likely cause of the container OOM at ~12:50Z. Games in flight at the abort: gi 1457, 1827, 2157, 2179, 2291, 2417, 2445.
Galadriel is NOT requeued: root-cause the oracle's memory on X-spell payment enumeration first (per-game RSS with
`MTG_MANA_ORACLE=2`, one thread), then rerun the deck alone.
