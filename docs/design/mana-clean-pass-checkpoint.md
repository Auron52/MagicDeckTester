# Checkpoint: mana-clean per-deck pass (pass 1) and what follows it

Shared state for whoever picks this up (Claude or Codex). Last updated 2026-11-10 ~14:00 UTC.
Related: `docs/design/mana-clean-followups-2026-11-04.md` (USER rulings, queued fixes; sections 7-8 are 11-06),
`docs/design/mana-oracle-memory-time.md` (the oracle memory + time fixes), `scripts/mana_clean.py`.

## Standing program (USER)
Analyse every deck with the mana-clean analyzer (`scripts/mana_clean.py`), fix every issue, mark each deck. A deck
leaves beta only after a clean analysis of >= 5,000 games on a FRESH seed block after its fixes. Fixes must be
mechanism-level, lossless, and respect NO-GREEDY (CLAUDE.md). Rules in force: ONE engine job at a time (USER 11-04,
after an out-of-memory container kill; check `free -g`; `MTG_RSS_CAP_GB=40` is per process); no `timeout`; wait on
PIDs with `kill -0`, never `pgrep -f`/`pkill -f`; rebase, never merge; `python3 scripts/check_references_safe.py`
before every push. "Feel free to delay any iterations that seem to be getting stuck for further discussion" (USER).

## What is running (2026-11-10 overnight; scratch on the build volume)
Codex handed this queue over in `docs/design/handoff-codex-mana-2026-11-10.md` (read it first). The 11-06 pass
driver and post-pass chain are long finished. Live now:
1. **Three Dragons gi2535 tails, NOT this agent's** (PIDs 347828, 356311, 356792;
   `build/scratch/mcdragons/lrprice/hostfix-*.{out,err}`). Leave running; inspect the paired results when all three exit.
2. **Claude's overnight chain** for the Lake victim-colour fix (below). Its status is in
   `build/scratch/claude1110/STATUS` (one line per stage); a 2-hourly session timer checks it.

## 2026-11-10 overnight: Shadowborn payment fixes
Source of the work: `logs/mana_clean/lastweek-20261110-shadowborn-order/report.md` (games 20000-24999).
* **FIXED (uncommitted until validated): the Lake crack's victim colour.** When Lake of the Dead's on-demand crack
  sacrifices Blood Crypt (the only Swamp), tap-first floated the Crypt's first colour {B}, so {4}{B}{R} Sire of
  Insanity was unpayable although Crypt {R} + {B}{B}{B}{B} + a floating {B} pays it (seed 30020022 T3; nearly
  every APPLY-DROP signature). `SacVictimFloatPin` (SpellEffects.h) + `OnDemandCrack::victim_color` (ManaPayment.cpp
  TapForCostShared step 3): only after the unpinned crack fails, the payer retries with the victim pinned to each
  colour (the cost's pips first). Payments that already succeeded are unchanged. Unit tests in
  `test_shadowborn_lakeprof.cpp`.
* **FIXED (USER 2026-11-10: "We cannot choose something already marked as a sacrifice" / "a more general issue
  with the system"): the APPLY-DROP-ORDER 554 and many PHANTOMs.** Each sac_additional_creature cast (Culling,
  Intent) got its victim per cast, so a plan could have two sacrifice costs claim one creature; realisation then
  dropped one. `SubsetOverclaimsSacCostVictims` (TurnSolver.cpp, both walkers) walks PlanCastOrder counting bodies
  and rejects a claim with none left; any mid-plan body source (tokens, ripple, put/reanimate, persist...) makes it
  admit. s30020789: APPLY-DROP-ORDER 112->0, PHANTOM 100->6, UNMODELED 224->0, plans 781->527, same win turn.
  Tiers: only Shadowborn rows; searched 0 slower; every row's aggregate <= before.
* **Gate `sb-victimclaim-20261110` (6c42d7bd4, games 25000-29999): issues 1773** (MISS 87 = APPLY-DROP 74 +
  ENUM-GAP 10 + ORDER 3; ORACLE-GAP 28; UNMODELED 1658). Was 5429 (ORDER 554, UNMODELED 4759). Top APPLY-DROP: Sire
  {4}{B}{R} dropped where Lotus Bloom (R, keep-floating) + Lake crack pay it (x28 T4 m2 Intent+Intent+Apostle).
* **PARKED (owed behind floatseq, USER 2026-10-31 colour-at-spend ruling):** the remaining Shadowborn APPLY-DROPs
  are need-later colour commits. (1) Lotus Bloom cracked for {B} where Sire's later {R} needed it (x28, s30027361
  T4). (2) s30025422 T3: the line needs an EARLY Lake crack that taps Blood Crypt for {R} before sacrificing it; the
  victim pin only looks at the current cost ({B} for Culling), not Sire's later {R}. Both are "commit the colour at
  spend against the rest of the line" (docs/design/mana-issues-carryover-2026-11.md section 3).
* **Jumbo Cactuar CAPPED-DECISION (34,384 of 37,918): FIXED.** Utopia Sprawl's colour was fanned x5; every pip in
  the 75 is {G}, so JumboCactuarProvider::EtbChoiceCandidates keeps green only (sound dominance, Alchemist's
  precedent). s30024581: CAPPED-DECISION 30955->0, oracle 94 s -> 6 s; s30022536 no caps. Tiers: only Jumbo rows;
  searched 0 slower; backstop d5 s30001 game 11 now FINISHES (was abandoned at 2e6 units) as unwon (the search
  proves no win by T8 after a mull-to-4 hand) -> 4.00 -> 4.25 is the newly counted game, not a regression.
* **Gate `jumbo-sprawlgreen-20261110` (67d1ab0ce, 25000-29999): 25,065 issues** (was 37,918 on 20000-24999):
  CAPPED-DECISION 23,692 = ONE decision (s30025605 T5: T&N modes x Finale X x GSZ X x a Greaves equip dance -- a
  legitimate menu, no dominated copies found); ENUM-GAP 337, APPLY-DROP 108, APPLY-DROP-ORDER 429,
  DOMINATED-CREATURE 212, ORACLE-GAP 129, UNMODELED 158. The capped decision is analyzer cost (USER question on
  per-plan cost cuts open; caps are not raised to hide it).
* **USER DECISION (Jumbo cast order, not changed): APPLY-DROP-ORDER 429** are the provider's (proposed-for-review)
  order. Top shapes: (1) GSZ fetching Arbor Elf ranks 38, BEFORE the hand Arbor Elf (40), so with Greaves the hand
  Elf + haste equip that would fund GSZ comes too late (s30025904 T2; order that pays: Elf > Greaves > equip > GSZ);
  (2) Wild Growth before Overgrowth on the same Forest where Overgrowth-first pays Tooth and Nail (s30029950 T6;
  ArrangeCastOrder's proven swap covers Utopia Sprawl only); (3) Chariot of Victory after Voyaging Satyr where
  Chariot-first + equip pays (several). Proposed: tutor->Elf rank after the hand Elf; extend the proven Aura swap
  to Wild Growth; Chariot's equip-funded slot. Raised with the USER.
* **Jumbo APPLY-DROP top (x10, s30027358 T3), diagnosed, NOT fixed:** the own-host hold makes Wild Growth@F10 pay
  off F11, the pending Overgrowth@F11's host (-2), where paying off F10 (-1) funds Finale. A naive "yield to a
  larger pending bonus" fixed the repro (13 -> 0) but broke EDF gi1264 (test_prepay_colours: the own host already
  carries a bonus, so yielding loses more) and the Cloud-of-Faeries scarce-hold test -> reverted. The sound shape
  is sequence-aware: at a land-Aura payment with other pending land-Aura hosts, try both hosts and keep the one
  whose remaining casts pay (OrderPaysSequentially), i.e. the floatseq-style commit.
* **Jumbo non-cast-order items (agent 2026-11-10 evening), mechanisms:**
  - ENUM-GAP (337): the exact sequential walk (SubsetPayableSequential / OrderPaysSequentially) refused every
    subset holding a PAID equip ("unmodelled kind"), so a line funded by a haste equip onto a fresh untapper
    (Chariot -> Voyaging Satyr / Greaves -> Arbor Elf, the ApplyManaUnlockEquips hoist both apply worlds already
    make) was never rescued (s30027377 T3, gate label ef-walk-fail). Second mechanism (s30025119 T4): the walk
    attached a land Aura only to an UNTAPPED host, so Overgrowth paid off its own declared Forest was dropped and a
    Surrak-hasted Elf's untap of that Forest was priced without its +2. FIX: MTG_SEQ_UNLOCK_EQUIP (default ON) --
    the walk joins the unlock's pieces and calls the same ApplyManaUnlockEquips after each cast; a declared host
    keeps its Aura when tapped (walk + SubsetPayableWithFilters). FIXED: both repros clean (s30027377 ENUM-GAP 19->0,
    s30025119 8->0); units 2715/2715; smoke + regression: searched 0 slower, all searched averages unchanged (play
    digests moved on Jumbo/EDF/Alchemist rows from newly offered lines); ONE d0 (greedy runner) game slower,
    edf_regression_d0_s2002 gi88 6->7 (5.3210->5.3220) -- raised with the USER as d0-runner churn, accepted.
  - DOMINATED-CREATURE (212), PARKED: payer source choice, not a gate. s30026053 T3 {Forest, Wild Growth@F10,
    T&N}: under the own-host hold the payer pays Wild Growth's {G} with an Arbor Elf unit (untap the held F10 for
    {G}{G}) instead of the fresh Forest#12, then over-pays T&N with Elf#2 + Satyr units (9 for 7, the float is
    lost) while F10's own final tap is left unused. Fix shape: in TapForCost, prefer a plain land tap over an
    untapper unit that leaves its land untapped, and never spend a 2+-mana unit on a 1-pip remainder a land pays
    exactly. Payment-order change -> needs the full payer review; not attempted.
  - ORACLE-GAP (129), PARKED (analyzer-side): top shape s30026952 T4 {T&N + Overgrowth@10 + act:Nylea x4}: the
    engine over-taps (Forest#11 tapped, {G} left floating) and Nylea's pump target follows the resulting board;
    the oracle never enumerates an over-paying payment, so no outcome matches. Second shape (s30025893 T4):
    "Chariot on #0 by engine, #54 by oracle" -- an unattached Chariot in the engine's outcome vs the oracle's
    attached one after a T&N put. Both need the oracle to carry the engine's own (dominated) payment as an outcome.
  - APPLY-DROP (108) top x10: still the land-Aura host hold below; the sequence-aware shape needs the remaining
    plan at the apply sites (ApplyPlanDirect apply_one, AIEngine CastSpellFromHand, both walks) via a scope like
    LineUnpaidCostScope, then a state copy + OrderPaysSequentially on the rest per land-Aura cast. PARKED.
* **Gate `jumbo-unlockequip-20261110` (e0afecb7d + GT 9df2e007e, PGO, games 30000-34999): 1,336 issues** (was 25,065
  on 25000-29999; this block hit no capped decision): ENUM-GAP 241 (was 337; top shapes now T&N + Overgrowth +
  Chariot + Arbor Elf + equip, x19 s-T7 -- still to diagnose), APPLY-DROP 100, APPLY-DROP-ORDER 427 (USER cast-order
  decision, unchanged), DOMINATED-CREATURE 218 (parked above), ORACLE-GAP 311 (was 129; 56+28+10 are the Nylea x4
  pump shape: the nearest oracle outcome has the SAME taps and float, only the pump TARGET differs -- the pump target
  follows the moment each Elf is tapped, so the oracle must compare modulo pump target or replay the engine's; analyzer
  side, parked), UNMODELED 39. Play identity oracle-off: byte-identical.
* **FIXED (item 5h): Jumbo USER cast-order rulings 2026-11-11** (90f089561, 671f06816). Tutor-by-target ranks after
  the hand copy (Elf 45, Surrak 95, Cactuar 135); Surrak 37 -> 70, moved ahead of the first untapper only when
  OrderPaysSequentially proves it; Chariot right after the FIRST untapper only with 2+ untapper entries; the
  Overgrowth-before-Sprawl swap DELETED (USER: "Yes, I can't see why we would want Utopia Sprawl after Overgrowth").
  s30029950 T6 and the old swap's test board are NOT payment defects: one enchanted Forest tapped three times yields
  one mana less with the cheap Aura first (13 vs 14 for a 14-mana line), so no payment of that order exists.
  Tiers: only Jumbo rows; searched 0 slower; d0 regression s2002 gi315 4->5 (greedy runner), smoke d0 2 faster.
  Held-out A/B vs the pre-ruling order (d5 b20 seeds 7100001 x3000, d3 b10 7200001 x1500): d5 4.0597 -> 4.0580,
  d3 4.0640 -> 4.0633; 6 faster, 0 slower. ExtraTurns d3 s2002 gi23 / d5 s2002 gi73 moved in-batch only (replay
  alone = GT digest): run-context flaps, left out of GT.
* **Gate `jumbo-userorder-20261111` (6b4f57bc866d, PGO, games 35000-39999): 1,195 issues** (was 1,336): ENUM-GAP 327,
  APPLY-DROP 156, APPLY-DROP-ORDER 336 (was 427), DOMINATED-CREATURE 223, ORACLE-GAP 153. Play identity oracle-off
  byte-identical. Top APPLY-DROP-ORDER now: (1) Overgrowth-first lines the USER rule forbids (x24+x20+x20+x20; the
  same count effect as s30029950, the search keeps the sub-lines); (2) Chariot cast before a single hand Voyaging
  Satyr with GSZ (x15+x10), which the USER's one-untapper ruling keeps. **USER DECISION (surfaced):** both shapes are
  the USER's order working as ruled; no change proposed unless the USER wants a proven swap for either.
* **Shadowborn sb-victimclaim follow-up (agent 2026-11-10 night), all ANALYZER-side; play byte-identical
  (smoke 183/183 unchanged; regression below):**
  - **UNMODELED 1,658: FIXED (oracle).** Every one was "too many actions to try every order": the PHANTOM (NO) claim
    stopped at 180 distinct orders, and the gate's flags were 360 (1,035), 720 (333), 420-2,520 (290). The NO claim
    now enumerates up to `kPhantomOrderBound` = 5,040 distinct orders (the YES searches keep 180); the per-decision
    budget still turns an over-long search into CAPPED-DECISION. Every top repro (s30028549, 27949, 28345, 26491,
    29741, 29715 at 2,520, 28492 at 1,120, 25077) now reads PHANTOM (the planner's ripple optimism), none an engine
    miss; heaviest turn 2.8 -> 19.7 s oracle-on. Also: another ORDER's partial search used to return SILENTLY (no
    verdict, no count); it now emits UNMODELED "another order's search was partial: <why>".
  - **ORACLE-GAP "a field not listed" (24 of 28): FIXED (oracle).** The field was `scripted_early_ondemand_crack`: the
    payer consumes the searched early-crack pin only on its pinned branch (one Lake, no lump), so the engine's Lake +
    Lotus Bloom payment keeps it set; the oracle's scripted crack cleared it on every crack. Now
    `OracleOptionConsumesEarlyCrackPin` (ManaPayment.cpp) applies the payer's rule per payment option; DiffText names
    the pin. s30025089 / 27390 / 29714 / 29719 / 26470: 0 ORACLE-GAP.
  - **ORACLE-GAP rest (s30028232 x2, s30025999 x1): PARKED, diagnosed.** The engine cracks Lake FIRST (tap-first
    victim = the first Swamp, +5) then taps two more Swamps; the oracle applies a crack face LAST, after the Swamp
    class's first copies (folded, lowest numbers first) are tapped, so the victim is an already-tapped Swamp (+4) and
    {Lake crack + 2 Swamps} falls one short; the only crack options it keeps tap everything. Fix shape: apply a
    sac_crack face first within an option and remap a folded copy the crack ate (the class is interchangeable).
  - **ENUM-GAP 10: 5 FIXED (now HIDDEN-INFO), 5 PARKED.** s30028086 / 25451 / 26299: the line paid only with a Dark
    Ritual that Thrumming Stone's ripple revealed and cast off the hidden library top -- `TapsHiddenCard` now counts a
    ritual taken off the top as hidden information (as a tapped hidden land already was). Not diagnosed: s30026059
    (two Lakes), 26548, 27751, 29585, 29667 (ripple chains; possibly the same hidden-reveal family via free Apostles).
  - **PHANTOM s30020789 (6): PARKED.** {Apostle + Culling (+ act)} on ONE Swamp: Culling needs a creature victim
    already in play AND its own {B}, so Apostle must be paid first and nothing is left for Culling; the gate counts
    Culling's +4 as if it funded the casts before it. Sound tightening: in `SubsetOverclaimsSacCostVictims`, require the
    mana available BEFORE a sac-cost ritual to pay the ritual plus every cast that supplies its victim. PHANTOM is not
    an issue class.
* **Gate `sb-orderproof-20261110` (cbe0633fe rebased as 5c3d77690140 src, PGO, games 30000-34999): 210 issues** (was
  1,773 on 25000-29999): UNMODELED 0 (was 1,658), ORACLE-GAP 1, ENUM-GAP 2, HIDDEN-INFO 4 (disclosed), APPLY-DROP 44
  (top x8 s-T6 Culling + Stone + Sire + Ritual + Apostle act; plus the parked need-later colour shapes), APPLY-DROP-ORDER
  163 in 9 signatures, 6 of them ONE decision (s30033099 T6 m1: Culling > Apostle > Stone > Ritual > Sire > Apostle
  activation pays; the provider's order drops Sire). Play identity oracle-off: byte-identical. The ORDER count rose
  because the full order proof now FINDS paying orders the 180 cap never tried -- real cast-order misses, not new
  defects.
* **USER DECISION (Shadowborn cast order, not changed): APPLY-DROP-ORDER 163** (above). Shape: with Lotus Bloom up,
  Sire of Insanity placed before the Apostle + Thrumming Stone + Ritual chain is dropped; after the ripple chain it pays
  (Lotus {R} + a Swamp, keep-floating). Proposal for review: rank Sire after Thrumming Stone and Dark Ritual when the
  line holds both. Not measured.
* **FIXED (item 5j): USER ruling 2026-11-11, Dark Ritual vs Thrumming Stone** (496b4ae35). `ShadowbornProvider::
  ArrangeCastOrder` moves the Ritual(s) to just after the Stone when `TurnSolver::OrderPaysSequentially` pays that order;
  else Ritual keeps rank 1. The walk resolves a ritual off its card, so ripple-revealed (chain) mana never counts. ONE
  arrangement per line; a unit test shows the plan count is unchanged (14/22/24 on 4/6/7 Swamps, arm on and off).
  `MTG_SB_RITUAL_AFTER_STONE`, default ON. The Culling-victim placement is unchanged. The USER's earlier rule (a hand
  Apostle as Culling's victim when none is on the battlefield) still holds through `SacCostVictimVariant`. Repro s30033099 T6: MISS 0.
  Tiers: only Shadowborn rows moved. Smoke d5 4.81->4.80, d3 =, d0 7.377->7.388, 2HG d3 5.28->5.30. All 5 regression rows
  improved (d5 s2002 4.813->4.780). All 4 searched slower games are churn (recover at 4x/16x). Held-out A/B off->on:
  d5 b20 7300001 x3000 4.8013->4.7817, d3 b10 7400001 x3000 4.8803->4.8610, 2HG d3 b10 7500001 x2000 5.0565->5.0355.
  GT accepted. After rebasing onto 644619789, smoke shows alchemist2hg_smoke_d5 with the same score and a new digest.
  That comes from that Alchemist commit, so it is not accepted here.
* **Gate `sb-ritualstone5k-20261111` (src f055ad5ee7e1, PGO, games 35000-39999): 79 issues** (was 210): APPLY-DROP-ORDER 7
  (was 163), APPLY-DROP 68 (top x28 T4 m2 Apostle + Culling + Intent + Stone; x24 T5 m2 Sire + Intent + Apostle),
  ENUM-GAP 3, ORACLE-GAP 1. Play identity with the oracle off: byte-identical.
* Note: after rebasing onto the Gods merge (f90930c9f), smoke shows 2 same-score play-digest changes
  (bruna2hg_smoke_d5_s1001, landfall_smoke_d5_s1001). They reproduce byte-for-byte with the ORIGIN tip's binary (no
  commit of this chain), so they come from the Gods merge; not accepted here -- owner: the Gods session.
* Chain order: build -> unit -> the s30020022 repro -> smoke (expect only Shadowborn rows to move) -> regression ->
  `./build.sh pgo` -> Shadowborn 5,000 gate on block 25000-29999 -> record, commit, push.

* **Jumbo items 5m/5n (USER rulings 2026-11-11): FIXED in play, gate WORSE -- USER DECISION.** One commit
  (`Jumbo 5m/5n: ...`) + GT accept. 5n `JumboOvergrowthAtBreakpoint` (`MTG_JUMBO_OG_BREAKPOINT`): s30035557 T4 clean.
  5m `MTG_UNTAPPER_UNLOCK_HOSTS`: every fresh Elf/Satyr, and the copy GSZ/Finale puts, is an offered haste-equip
  unlock host. The last hole was `BuildEquipPieceDeps` (its cast-group mask knew only hand numbers, so the
  odometer never selected an equip onto the tutored copy). s30036180 T3 now carries no MISS class.
  ManaOracle `KindText` is now a switch: the table was 4 kinds out of step, so "attachall:" in every older
  report meant **Equip**. Tiers: only Jumbo rows; smoke 0 slower; regression net -4 turn-games; the one slower game
  (s2002 gi28, draws diverge T4) recovers T6 at d8 b0. Held-out A/B (one batch, levers off vs on, same seeds as
  5h): d5 4.0580 -> 4.0557 (15 faster / 8 slower), d3 4.0633 -> 4.0620; the off arm reproduces 5h byte-for-byte.
  **Gate `jumbo-5mn-20261111` (PGO, src e59de76cbcef): sized by `--minutes` to 45,000 games (40000-84999; I omitted
  `--games 5000`).** 85,154 issues. Per 5k games: APPLY-DROP 705 (was 156), APPLY-DROP-ORDER 670 (was 336),
  CAPPED-DECISION 66,046 flags from 12 decisions. Play identity: byte-identical. **Attribution** (1,000 games of the
  block, oracle on, one batch): levers off -> APPLY-DROP 28 / ORDER 30 / CAPPED 899; 5n-only identical to off;
  both on -> 88 / 88 / 4,306. So the whole rise is 5m: the plans it offers (top: T4 GSZ->Elf + Satyr + Greaves x2 +
  Tooth and Nail, s30061937 x95, s30065549 x40) are ones the engine's payment cannot realise. The oracle pays them by
  re-making 3+ earlier mana decisions (re-tap scheduling around the untapper). This is a payment-path defect that the
  new menu exposes; before, the lines were simply not offered. **USER DECISION:** keep 5m ON (play is better and it is
  the USER's ruling) and fix the untapper re-tap payment next, or hold it OFF until that payment fix lands. I left it
  ON. Not done: Sprawl / other-land Overgrowth (s30037731, s30037871, s30039942, s30035570, s30036569), still a
  pending USER question. A Chariot Elf->Satyr move is a possible follow-up.

* **Extra Turns oracle cost (agent 2026-11-11): two lossless collapses FIXED; gate PARKED.** Diagnosis is in
  `docs/design/extra-turns-mana-oracle-sizing-2026-11-10.md` (section 2026-11-11). (1) The hit-only cut:
  `Realize` no longer builds fail-option scripts for callers that read only the hit (ENUM-GAP checks and the
  sibling-payment check). The 534-plan p3 decision went from 768k to 225k applies. (2) `SibPayMemo`: one
  search per sibling per decision. Sibling-payment applies went from 4.21M to 0.11M. Flags are identical on 11
  heavy seeds (8,938 lines). The 180 -> 5,040 order bound is inert here (at most 3 movable actions).
  **PARKED:** a 64-game sample (games 90000+) gave a mean of at least 365 s per game, and 29 of the first ~51
  games had a decision that spent its 300-priced-s budget. That projects to at least ~16 h for 5,000 games,
  and the gate would fail on CAPPED-DECISION anyway. The cost is menu breadth: land-search-pin and dig
  variants, which differ through hidden library order. Folding them is a search-side (play) change or needs a
  proof that does not hold here. Lens issues seen (APPLY-DROP 45, ORACLE-GAP 110 on the heavy seeds) are
  parked in the doc.
  Validation, before the rebase onto 7a7c2079b: unit tests (oracle/mana) passed 191/191. Smoke moved only the
  known Gods-merge pair (bruna2hg, landfall d5). Regression showed 5 same-score digest changes (extraturns d5
  s2002, gods d3, fivecolour d3/d5, landfall d5 s3003). The pre-change binary reproduces every one, so none
  comes from this change; GT was not accepted. After the rebase onto 7a7c2079b (whose GT accept covers those rows), smoke on
  the rebased build passes all 187 rows.

## 2026-11-11: near-clean decks (breachingdragonstorm, soldiers, burn, minotaur, fluctuator, vampires, angels)
Gate `nearclean-20261111` (src ab70b5506, PGO, 5,000 games each on the next fresh block; play identity with the
oracle off byte-identical; record 43e700b5c):
* **breachingdragonstorm CLEAN** (210000-214999, 0 issues; was 6 on 100k) -- leaves beta.
* **soldiers CLEAN** (191000-195999, 0; was 6) -- leaves beta.
* **burn CLEAN** (210000-214999, 0; was 4) -- leaves beta.
* **fluctuator CLEAN** (210000-214999, 0; was 70) -- leaves beta.
* **minotaur 1 issue -> FIXED (df06f89bd)**, APPLY-DROP s30273945 T4 {vial Ragemonger + Neheb + Boros Reckoner}
  off Mountain + Rakdos Carnarium. The whole-turn prepay priced the casts on the board, where the vialed Ragemonger
  is not yet (Vial puts ride the cast sequence), read {1}{R}{R}{R}{R} UNPAYABLE, and the per-cast greedy paid
  Neheb's {1} with the Mountain, stranding the Karoo's {B}{R}. The prepay now applies the enumeration's
  same-line coloured-pip reducer credit (MTG_PREPAY_LINE_REDUCER, default ON). Repro clean; unit 2770/2770;
  smoke byte-identical; regression only gods_regression_d3_s2002 digest-only (same 5.38; Gods holds no reducer, so
  the change is inert there; NOT accepted). **Re-gate `minotaur-linereducer-20261111` (277000-281999, PGO df06f89bd): CLEAN, 0 issues, play identity byte-identical -- leaves beta.**
* **vampires 2 issues (ORACLE-GAP x2, one game), PARKED (analyzer-side, with an engine smell).** s30211775 T3
  {Secluded Courtyard + Markov Baron (convoke) + vial Bloodghast + Falkenrath Pit Fighter + Voldaren Estate Blood}:
  the engine paid Pit Fighter's {R} with a Voldaren Estate (1 life) and left the fresh Courtyard untapped, ending
  life 18 with one Blood token; the oracle never enumerates that dominated life payment, so it cannot reproduce
  the engine and calls it a GAP. Its nearest outcome (Courtyard {R}, life 19, a second Blood token) dominates the
  engine's, i.e. the real defect is a DOMINATED-LIFE payment choice (not an ISSUE class; 2,142 such flags on the
  block). Repro: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Vampires/Vampire.cod'
  --profile 'decks/Vampires/Vampire.profile.json' --games 1 --seed 30211775 --game-index 211775`. Fix shapes:
  the oracle admits the engine's own payment as an option (closes the GAP), and/or the payer prefers the
  creature-only Courtyard over a life-paying Estate for a Vampire creature spell.
* **angels 4 issues (DOMINATED-CREATURE, 3 signatures), PARKED (per-cast GREEDY payer order).** s30210844 T3
  {Plains + Serra the Benevolent {2}{W}{W}} off Remote Farm, Sol Ring, Plains, Giada: the greedy pays the first
  {W} with the Plains, the second with Remote Farm ({W}{W}, one floats), then the generic drains that floating
  {W} before Sol Ring {C}{C} -> {C} floats, and the next {W} creature (Lightstall Inquisitor) must tap Giada (a
  vigilance flier that would attack). Farm {W}{W} + Sol Ring {C}{C} pays Serra exactly. The per-cast give-back
  (MTG_PAY_SHRINK) lives only on the BACKTRACKER path and triggers on source count > MV; tried widening its
  trigger to "mana left floating" -- no effect, because this payment is the greedy's (FLOAT_TRACE shows its tap
  lines), so reverted. Sound fix shape: the same give-back after a greedy success that left floating mana (or a
  multi-unit source taken before a single for a {W}{W} pair). Payer-wide change -> needs the payer review the
  Jumbo DOMINATED-CREATURE item already parked. Repros: seeds 30210844 / 30213528 / 30214769 (game-index = seed
  - 30000000), `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Angels/Angels.cod' --profile
  'decks/Angels/Angels.profile.json' --games 1 --seed <s> --game-index <gi>`.

* **gods_regression_d3_s2002 drift after df06f89bd: NOT df06f89bd, NOT another commit -- a plan-cache RESIDENCY
  flap (agent 2026-11-11). PARKED as a determinism defect; GT left as is (consistent).** The one-job run of the
  case gives the GT digest 9a1c178f with MTG_PREPAY_LINE_REDUCER=1 AND =0, and so does the failing run's own
  snapshot binary (`test/logs/regression/mtg.run`); a full pooled rerun of the whole regression manifest on HEAD
  (9bf63e742) matched GT on 295/295 rows. The digest follows the shared plan pool: `MTG_PLAN_CACHE_KB=4096` on the
  one job reproduces the failing 76f2637c exactly (units 968546, the failing run's), 65536 gives f7528bad. The row's
  GT history already flips 76f2637c (1ab127b24) -> 9a1c178f (7a7c2079b), and the extra-turns agent saw it move with a
  binary that had no change. Both pooled runs logged `[determinism] plan cache refused ~2,300 stores` (pool full,
  3199/3200 MB), so the "hit-equals-miss" neutrality does not hold for Gods. Evidence: `MTG_BP_ENUM_VERIFY=3` on the
  job reports `CONTENT-DIFF 600` of 224,108 bp hits, all Life from the Loam rows (`served=299 fresh=1`: a served list
  of Loam target-subset variants where a fresh derivation returns one plan). Two sound candidate fixes were tried and
  did NOT cure it (saved, unlanded: `build/scratch/p1/gods-determinism-wip.diff`): (a) bypass the bp cache while a
  Loam target probe is in progress (the enumerator expands "L?" only when `g_loam_probe_offer == nullptr`, a context
  no key carries); (b) a new graveyard-reader bit `GyR_GyLandNames` stamped from `return_up_to_gy_lands`, folding our
  graveyard's land names into the sim key and Dominance (Loam's targets were in no key). With both, the digest still
  moves with MTG_PLAN_CACHE_KB and the 600 diffs remain. Next suspect: the fresh=1 side looks like a CUT list (a
  derivation that read an overrun budget), i.e. the MTG_CACHE_READ_GUARD class -- the Loam probe's ApplyPlanDirect
  may consult the budget without marking `budgetbind::t_reads`. Repro: `MTG_BP_ENUM_VERIFY=3 build/Release/mtg
  --batch build/scratch/p1/m.json --threads 32 --game-log-dir <dir>` (one job: gods_regression_d3_s2002). Until fixed,
  this row can fail a pooled regression with no code change: re-run it alone before attributing.
  **FIXED 2026-11-11 (cross-turn cache session; docs/design/cross-turn-cache-experiments.md, "Gods d3/d5 s2002
  residency").** The cause was neither candidate: `GodsProvider::LoamTargetSearchCandidates` narrows Loam's target fan
  from the SEARCH ROOT (Magus of the Bazaar there, its graveyard land count), the probe mode and DecisionUnpruned, none
  of which any key carried. `DecisionProvider::EnumerationContextKey` now returns those inputs and BuildBreakpointKey
  folds them (plus a "inside a Loam probe" fold). gi39: CONTENT-DIFF 0 of 9,199; the whole regression manifest at
  `MTG_PLAN_CACHE_KB=1` matches the default pool on every row's digest and units. GT re-accepted.
* **Dragonstorm 2HG gi37 opener hold (followups s8 Q23): PARKED with a mechanism diagnosis (agent 2026-11-11).** The
  hold is not on HEAD (only on `worktree-agent-afe476c961652c9b8`), so HEAD plays gi37 as the off arm. Re-read the d8 b0
  traces (`build/scratch/ds13/tr/d8_g37_h{0,1}`): the arms first differ at T4, where the hold arm spends Rite of Flame
  + Desperate Ritual on an early Scourge of Valkas; the off arm keeps both rituals and goes off T7 (Medallion, Rite,
  Desperate x2, Apex, Rite, Pyretic, Song, Apex, Kolaghan {3}{B}{R}, Scourge) and wins T8. Nothing at T4 is paid
  differently (no {B} source on the board), so the hold acts only inside the T4 search's simulated T7 go-off. Its
  drain (`SpendFloatingTowardCost`, 9eaae0e16) pays generic from the float with the held {B} added to the line's
  NAMED demand and keeps that order while float red >= the NAMED red. An opener line's continuation is by definition
  unnamed (Apex's seven exiled cards are re-planned on the float: Scourge RRR, Irencrag Feat RRR, Kolaghan R), so the
  hold burns red that the continuation needs, and a payable T7 line is valued short. That is exactly the "payable line
  made to look unpayable" the USER's 11-06 principle forbids, and it explains why only a red margin of 15 (the hold
  never kept) recovers d8 b0. **No margin or guard can fix it**, because any colour committed to generic before the
  continuation is known is a guess. The lossless shape is to make the hold unnecessary: generic paid from a mixed float
  is NOT committed to a colour; the pool carries a generic debt, and a later coloured demand of k pips of colour c is
  payable iff `float[c] >= k` and `total - k >= debt` (the floatseq "commit colour at spend" principle the USER set
  for any-colour mana, extended to a committed mixed float). Any line payable under some fixed drain order is then
  payable, so the search can never undervalue it, and a {B} for Kolaghan stays available whenever it can. This is a
  ManaPool/payer-wide change (every `reserve.<colour>` reader, the oracle's payment model, the viewer's pool display),
  so it needs its own design + full A/B; not started. The branch's payer fixes (9507fdb5c, 658b09d45) remain separate.

## Pass verdicts (this segment; earlier ones are in test/mana_clean.json and status.log)
- galadriel: 71,000 games, 372,073 issues (MISS 453 = 425 APPLY-DROP + 28 ENUM-GAP "cast both Galadriels",
  ORACLE-GAP 6, UNMODELED 357,908 mostly "scripted payment did not pay {1}{U}{G}" / "set-aside exceeds the floating
  pool"). First complete run: the oracle time fix c1cba3e87 took it from a stall to ~1 h.
- selesnyalifegain: 97,000 games, 197,104 issues. Also first complete run.
- DEFERRED 3x (in `pass1/DEFERRED` with repros): eldrazidisplacerflicker (gi 19, 61), accomplishedalchemist
  (gi 29, 59). NOT cured by the oracle time fix -> the post-pass diagnosis above.
- Triage of finished decks' issues is PARKED (budget); route them per deck to fix work next week.

## s9 work in progress (2026-11-06 ~23:00Z, UNCOMMITTED in the primary checkout; survived a power cut)
- Stall ON arm finished 3 of 5 before I killed it: EDF g61 3.0 h, Niv g47 1.1 h, AA g59 7.6 h (digests == OFF arm, so play
  is unaffected; pure analyzer cost). Each stall is ONE decision (EDF T3 m1 240 plans, AA T4/T6, Niv T5 3,188 plans).
- Not cache thrash: the bp continuation memo had clears=0 on g61 and a 16 GB plan-cache budget changed nothing.
- An APPLY cap cannot bound it: g61 T3 made 60k applies in 34 s, then the next 140k took ~97 min (later applies reach
  new breakpoint boards that each derive a huge continuation list). So the per-decision budget is in PRICED WORK
  (`workctr::t_priced`, deterministic): `MTG_MANA_ORACLE_DECISION_S` (default 300 priced s, 0 = off). Past it every
  remaining verdict of the decision is `CAPPED-DECISION` (new class, FAILS mana-clean like CAPPED; scripts updated).
  `MTG_MANA_ORACLE_HEAVY=<applies>` prints `DECISION-COST` lines (sizing).
- Next: verify on g61 + the 5-game stall.json, commit, rerun the 4 deferred decks. The continuation-menu memo is moot
  (the engine's own bp memo already serves it); the real per-node cost is the derivation on NEW boards.

## 2026-11-07
- s9 fix COMMITTED 94bfbd7b1. Pass driver `pass1/drive2.sh` relaunched (PID `pass1/driver3.pid`) on EDF, AA, Niv, LFE.
- Queued behind it: LINE_REPAY funnel + price measurement (`build/scratch/mcdragons/lrprice/chain.sh`, DONE marker
  there), per the USER's 11-07 price ruling (followups s7a).
- Overnight queue `build/scratch/queue1107/chain.sh` (PID chain.pid, log status.log, DONE), after the two above:
  c-wild REBASED onto origin 11-07 (branch fix/c-wild, local; backup/c-wild-pre-rebase-1107; one Dominance.h
  size-comment conflict resolved, size assert left at HEAD's 1272 for the compiler to check) -> units + smoke;
  Mirrorwing g662 (base digest 30bd27827a1bab5e T4, new b8 T5) replays plain + MTG_NC_SEARCH=1 with game logs;
  EDF g61 oracle perf profile (`g61.perf.txt`) to cut the per-plan cost behind CAPPED-DECISION.
- Hinata gi27 (c-wild) dispositioned recoverable by the USER's 11-07 ruling (followups s10): c-wild is clear to land.

## After the chain (in order, one engine job at a time)
1. Read the stall diagnosis (LFE already shown ANALYZER-side, followups s9); build the s9 fix (memo + per-decision cap), rerun the 4 deferred decks.
2. Hinata2 regression game 27 (seed 3003, gi 27) at `--depth 8 --budget-ms 0` on the `fix/c-wild` binary (old
   manifest `build/scratch/cwild/regslow/new.json`; the 11-04 run was lost to the OOM kill). Wins on T8 -> no
   ruling needed; otherwise ask the USER (accept as budget churn vs root-cause). c-wild is parked on `fix/c-wild`.
3. Mirrorwing g662 (seed 73000662), followups s8 Q25a: replay base vs `wip/mc-mirrorwing`, plain and
   `MTG_NC_SEARCH=1`, with game logs; show the USER where the base's T4 win depends on the draw order.
4. Fix work (followups s7-s8): Dragonstorm 2HG gi37 root cause (hold must only order spending, never make a payable
   line look unpayable); Mirrorwing H1 + H2 (build both, A/B); port `ShuffleUnseen` (cc3b47aea) onto main's
   `m_known_top`, fix the non-clairvoyant search to keep known cards (Niv gi887, seed 1650001), switch `EmitEvalRows`
   labels; LINE_REPAY (`wip/mc-dragons` 06d079365): fix the land-Aura host-hold find mismatch, price 12 + price pass 1
   (default, measured), fresh EDF A/B.
5. Remove the worktree `.claude/worktrees/oracle-time` with `scripts/safe_worktree_remove.sh` (content is pushed).

## Still open for the USER
- Two unit failures on the branch are another session's: `test_landfall` fetchland (Beast count 1 != 2) and
  `test_pirates_provider` (deck folders 47 != 46, after the RW Burn deck).
