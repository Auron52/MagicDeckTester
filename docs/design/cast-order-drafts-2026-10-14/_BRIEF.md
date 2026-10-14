# Cast-order draft brief (shared by every per-deck drafting agent, 2026-10-14)

## Why
USER, 2026-10-14: *"We obviously need to review cast orders including Hinata's. The generic order is never a
good option in my opinion. It should be edited by you and then presented to me for additional feedback. It
might not hurt to also fix up the generic order, but the real solution is going to be to have an agreed upon
order for each. Major bonus points if that can largely be constructed by AI as well."*

Standing rules (binding):
* USER 2026-08-17: *"the order and range + 2nd vs 1st main should be reviewed by the user"* and *"ideally, AI or
  coded rules would help also to come up with a good approach, so that the review is easy."* You PROPOSE; the
  USER decides. Make the review cheap: a clear table, the reason per slot, and only a handful of flagged calls.
* USER 2026-10-04: falling back to the generic order when a USER order exists: *"there is no case where we should
  do that"*, and *"If I gave you an order you should try to figure out why it doesn't work and maybe suggest
  alternatives."* If the deck has a USER order that is OFF (it measured worse), your draft STARTS from the USER's
  order: find the recorded root cause (docs/design) of why it lost and propose the amendment that keeps the
  USER's intent while fixing that. Never propose "keep generic".

## How cast order works in this engine (read, then verify in code if you depend on a detail)
* `DecisionProvider::CastOrderRank(state, def)` -- LOWER = earlier. It sequences the casts of a plan WITHIN one
  main phase. The search chooses WHICH spells (the subset) and the land drop; the rank decides their order.
  Generic tiers (no override): land drop first, mana rock 5, creature 10, ritual 15, cast restrictor (Irencrag
  Feat: "you can cast only one more spell this turn") 18, other noncreature 20, on-cast self-damage 30 (last).
* `CastOrderLess` (src/ai/ManaPayment.cpp) stable-sorts by rank; ties fall back to cheapest-first among mana
  accelerants only, otherwise to ENUMERATION order -- i.e. a tie is an order nobody chose.
* THE ORDER IS A PRUNE. A line the order cannot express is unfindable at every budget. Example: with Irencrag at 18
  and Ponder at 20, a plan {Irencrag, Ponder, Crackle} casts Irencrag first, Ponder takes its one follower slot,
  and Crackle cannot be cast.
* RANGES: a card that impacts affordability may get `[ideal .. cost-efficient]` (MTG_ORDER_RANGE ladder): the ideal
  order is tried first and the card walks toward its cost-efficient end only when the line is otherwise unpayable.
* BREAKPOINTS: after a draw / reveal (cantrip, dig) the rest of the turn is re-planned with the new information,
  so "draw first" does not commit the rest of the turn.
* ORDERING-SEARCH decks (Dragonstorm: `WantsCastOrderingSearch`) search their combo order instead; their rank
  is the canonical first try.
* MAIN PHASE: each cast is classified Main1 / Main2 / Both (`ClassifyMainPhase`, provider `MainPhaseOverride`).
  The USER reviews this together with the order. Include the m1/m2 column; flag only the calls that matter.
* Principles already agreed (docs/design/cast-order-ideal-with-ranges.md): (1) draws before the land drop and
  before rituals; (2) cost reducers before the spells they reduce; (3) enablers before payoffs. Deck-specific
  knowledge beats these where the card text says so -- cite the text.
* Read docs/design/cast-order-ideal-with-ranges.md ("What the engine does today" through "THE REVIEW RULE") and
  your deck's section of docs/design/cast-order-rankings.md (if any) before drafting.

## Inputs for your deck
* The decklist (decks/<Deck>/<file>.cod or .txt) and its profile.
* Card data for EVERY card from src/cards/data/cards.json (mana cost, type, P/T, oracle text, params). Never
  from memory; Claude's card recall is unreliable. Use .claude/skills/mtg-rules.md for any rules question.
* The current effective order: /workspaces/MagicDeckTester/build/scratch/castorder/reports/<Deck>.txt
  (`mtg <deck> --profile <profile> --cast-order-report`, current tree). Its `provider:` line names the provider;
  the class is `<Name>Provider` in src/ai/DecisionProviders.{h,cpp} (Generic = GenericProvider). Read its
  CastOrderRank / CastOrderRankLatest / MainPhaseOverride and any flag that gates an authored order (grep for
  MTG_<DECK>_ORDER, *_CAST_ORDER, *_USER_ORDER, and EnvOn near the CastOrderRank). Note default ON/OFF.
* History: `grep -ril "<deck name>" docs/design | xargs grep -il "cast order"` and docs/design/analysis-<Deck>.md;
  any USER-quoted order or ruling there is binding input.

## Output: ONE file, /workspaces/MagicDeckTester/build/scratch/castorder/drafts/<Deck>.md
Concise; the USER will read ~34 of these. Sections, in this order:
1. **Status** -- one of: (A) USER-agreed order shipped default ON; (B) USER order exists but OFF (flag, date,
   why, measurement, where documented); (C) agent-proposed order never reviewed by the USER; (D) generic tiers
   only. One line of evidence each (file:line / flag / doc).
2. **What plays today** -- a compact table: position, card(s), main, the rule that put it there. Mark ties.
3. **Proposed order** -- table: pos | card(s) | main (m1/m2/both) | range (if any) | why (one line, from the card
   text and the deck's plan). Ties only where order provably cannot matter -- say why. Cover EVERY nonland card
   in the list, and say where the land drop goes (before / after the draws).
4. **What changes and why it matters** -- the 3-6 most consequential differences vs today, each with a concrete
   turn where the current order loses a line (a cast it makes impossible, a trigger it misses, mana it strands).
   If amending a USER order: the recorded root cause of its loss and how the amendment answers it.
5. **Questions for the USER** -- at most 5, each with your default answer, phrased so a one-word reply settles it.
6. **Generic-order findings** -- anything this deck shows is wrong with the generic tiers (for a generic fix).
7. **Implementation notes** -- which card PARAMS identify each slot (house style: providers rank by params, not
   names), what CastOrderRank code would change, and any engine limitation that blocks the proposed order
   (e.g. a conditional position a scalar rank cannot express).

## Pitfalls found by earlier drafts (read)
* `mtg` loads src/cards/data/cards.json RELATIVE TO THE WORKING DIRECTORY and, if it is missing, silently plays
  with no cards (zero lands, every game unwon, avg 9). ALWAYS run it as `env -C /workspaces/MagicDeckTester
  build/Release/mtg ...` (or pass `--cards-json /workspaces/MagicDeckTester/src/cards/data/cards.json`).
* Ties at one rank are decided by HAND order, which nothing searches -- a tie between cards whose order matters
  is a real defect. A single-game probe on a hand-built board shows it (Giants: Hamletback Goliath's counters).
* ORDER-OPAQUE sets (draw / staging / cascade / retrace / impulse / solo-target trick cards; ManaPayment.cpp
  `OrderingOpaque`) skip the rank sort entirely unless the provider opts in (`OrderOpaqueCastsByRank`), so for
  such cards the report's rank is NOT what plays -- plan (hand) order is. Check this for your deck.

## Rules for this job
* READ-ONLY on the repository. Do not edit, create or delete anything under /workspaces/MagicDeckTester except
  your draft file in build/scratch/castorder/drafts/. No git commands that write. No builds.
* You MAY run the existing binary for read-only probes: `MTG_PGO=0 /workspaces/MagicDeckTester/build/Release/mtg
  <deck> --profile <profile> --cast-order-report`, or a single game (`--games 1 --threads 1`, add `--log-dir` under
  /workspaces/MagicDeckTester/build/scratch/castorder/probe_<Deck>/`) to confirm a claim. No batches.
* No permission prompts: never `cd X && ...` (absolute paths only), never `rm`, put multi-step shell work in a
  script file under build/scratch/castorder/ written with the Write tool.
* Final message (all the integrator sees): status letter, the proposed order in one line, the top questions, and
  any USER order that is currently OFF.
