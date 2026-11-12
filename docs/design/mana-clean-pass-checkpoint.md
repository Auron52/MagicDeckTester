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

* **Jumbo item 5o (agent 2026-11-11): untapper payment defect FIXED; CAPPED-DECISION PARKED; ORDER rise = USER
  DECISION.** Commit `Jumbo 5o: drain a hasted untapper ...` (`MTG_HASTE_HOST_DRAIN`, heurarm slot, default ON).
  Root cause (s30061937 / s30065549 T4): `ApplyManaUnlockEquips` moved Greaves Elf -> Satyr as soon as the Satyr
  entered, with the Elf still untapped (Satyr was paid from GSZ's float), so the Elf lost haste and its re-tap of
  the Overgrowth Forest was stranded; Tooth and Nail dropped. Fix: before an unlock/release moves a haste
  Equipment off a fresh single-colour mana dork that cannot tap without it (proved on a copy; skipped when a later
  equip re-hastes it), tap the dork into the float (`DrainOutgoingHasteHost`, via `ApplyEnginePreTap`). Sound:
  mana only added, the dork could neither tap nor attack afterwards, no colour chosen. Unit test
  `drains a hasted Arbor Elf before Greaves moves` (fails with the lever off). 1,020-game oracle check (attr
  block 40000-41019, 5m on): APPLY-DROP 88 -> 25 (levers-off arm 28), ORDER 88 -> 83, CAPPED 4,306 unchanged.
  Tiers: only Jumbo rows, 0 slower / 0 faster (6 same-score line changes: the drained mana now funds Chariot /
  Overgrowth / Cactuar a turn early); after rebasing on 78e623dac both tiers byte-identical to the accepted GT,
  817 refs 0 drift. Held-out A/B (d5 b20 7100001 x3000, d3 b10 7200001 x1500): 4.0557 = 4.0557, 4.0620 = 4.0620,
  0 slower / 0 faster, 4+4 play-changed; the off arm reproduces 5mn-on byte-for-byte. Gate: **`jumbo-5o-20261111`** (PGO, src 6c2652cb9d1d, block 85000-89999, 5,000 games): 13,245 issues, play byte-identical oracle off. Per 5k: APPLY-DROP 82 (was 705), APPLY-DROP-ORDER 669 (was 670), ENUM-GAP 232, DOMINATED-CREATURE 295, ORACLE-GAP 150, CAPPED-DECISION 11,817 from 3 decisions (top s30086403 T4, a T&N + GSZ + Chariot + Finale menu). Remaining top APPLY-DROP: T3 Overgrowth@11 + T&N + Wild Growth@10 + Overgrowth@10 (x20), no equip -- a separate Aura-order shape. Next fresh block 90000.
  **ORDER rise (USER DECISION, cast order is USER-owned):** the residual 5m ORDER flags are T3 GSZ->Elf +
  Overgrowth + a haste equip (s30040847, s30040679, s30040145, ...): the ranked order casts Overgrowth before GSZ,
  but only GSZ -> equip -> Overgrowth pays (the hasted Elf's re-tap funds Overgrowth). Proposal: move the
  untapper-fetching GSZ (and its unlock equip) ahead of Overgrowth only when `OrderPaysSequentially` fails for the
  ranked order and passes for the swap (the 5h Surrak pattern). Not implemented.
  **CAPPED-DECISION (PARKED, oracle cost, not play):** in the 1,020 games only two decisions cap: s30040027 T4
  (2,950 plans, 2,296 unaudited; new with 5m) and s30040159 T4 (1,740 plans, 899 -> 2,010). The menus are the
  board's equip-host variants x ~276 Tooth and Nail pick/put variants per shape (T&N x2 + Satyr + Greaves 529,
  six shapes at 276); each tight two-T&N line (18 of 18 mana) is unpayable in the engine order, so it runs the
  full order search to its 2,000-apply cap and the rollback census. A lossless oracle collapse would key audits on
  the plan's mana-relevant projection, but T&N's picks are NOT mana-irrelevant (Surrak hastes dorks for the second
  T&N), so no safe fold was found; raising `MTG_MANA_ORACLE_DECISION_S` only buys verdicts. Repro:
  `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=4 build/Release/mtg 'decks/Jumbo Cactuar/Jumbo Cactuar.cod' --profile
  'decks/Jumbo Cactuar/Jumbo Cactuar.profile.json' --games 1 --seed 30040027 --game-index 40027`.

* **Jumbo item 5p (agent 2026-11-11): Utopia Sprawl FIXED; other-land Overgrowth + two-move case = USER DECISIONS.**
  Commit `Jumbo 5p: Utopia Sprawl joins ...` (9b94d41d7, local). USER (verbatim): *"Utopia Sprawl is essentially
  identical in this deck, so you can treat anything I say about wild growth here as applying to utopia sprawl."*
  `JumboOvergrowthAtBreakpoint` now moves the first Overgrowth before the first same-land Wild Growth OR Utopia
  Sprawl (conditions unchanged). s30039942 T4 and s30035570 T4 have no APPLY-DROP-ORDER now (lever on; off still
  flags). Units 2828/2828 (the old "keeps Sprawl before Overgrowth" board IS the breakpoint, so it now asserts
  the move with the lever on and the ranked order with it off); scenarios 459/459. Tiers: only Jumbo rows. Smoke
  searched 0 slower / 0 faster, 1 same-score play change (d5 gi109); d0 gi109 5->7: the moved T3 order pays one
  more cast (Finale of Devastation) and the greedy d0 runner spends it, losing T4's Tooth and Nail. That is the
  permitted greedy runner, not a payment defect. Regression searched 0/0/0, d0 gi315 5->4, 817 refs 0 drift.
  Held-out A/B (new binary vs the 5o on-arm, same seeds): d5 4.0557 = 4.0557 (0/0, 2 same-score), d3 byte-identical.
  GT accepted for both tiers. Gate: **`jumbo-5p-20261111`** (PGO, src de68d3583766, block 90000-94999, 5,000 games): 8,036 issues, play byte-identical oracle off. Per 5k (vs 5o block 85000): APPLY-DROP 38 (82), APPLY-DROP-ORDER 636 (669), ENUM-GAP 457 (232), DOMINATED-CREATURE 284 (295), ORACLE-GAP 192 (150), CAPPED-DECISION 6,429 from 1 decision (11,817 / 3). The ENUM-GAP rise is block variance: the top 11 ENUM-GAP seeds give identical counts with MTG_JUMBO_OG_BREAKPOINT 0 and 1. Overgrowth-before-Aura ORDER flags left: same-land Overgrowth-first 2 flags / 1 board (s30094939 T4, Overgrowth@11 + WG@11 + T&N + Greaves equip; a hasted-Elf line the sequential check does not price, not chased), other-land 15 / 3 boards, and the 5o GSZ-first shape 20 / 4. Next fresh block 95000.
  **Other-land diagnosis (s30037731, s30037871; T3, no untapper on board):** Forest#10 carries Wild Growth + Sprawl
  (3 mana), Forest#11 is bare (1). The line is GSZ X=1 (2) + Overgrowth@11 (3) + one more one-mana Aura@10 (1) = 6
  from 4 mana. It can only pay if Overgrowth enters on an UNTAPPED Forest#11, which then taps for 3. So every cast
  before Overgrowth has to come from Forest#10, whose 3 is exactly Overgrowth's cost. The ranked order (Aura@10,
  Overgrowth, GSZ) taps #11 or spends #10's mana first and reaches only 4 + 1. This is the 5n breakpoint with the
  host taken as Overgrowth's own land: outside (all but #11) = 3 = cost. The existing code already computes
  outside/stacked off Overgrowth's host; the only thing keeping it from firing is the same-host test on the
  Aura. **USER DECISION (not implemented; the USER said keep the original order for another land):** drop the
  same-land requirement on the Aura, so at the breakpoint Overgrowth moves before the first one-mana Aura on ANY
  land when the ranked order fails and the moved one pays (moved order Overgrowth > Aura@10 > GSZ pays 3 + 3 = 6
  on both repros). It only ORDERS: Overgrowth's host land stays a searched choice. Frequency: 2 boards / 10 flags
  in the jumbo-userorder 5k block, 1 board / 5 flags in the jumbo-5o 5k block. Both are T3 lines without an
  untapper, the case the USER said stacking is usually right because of the untaps.
  **Two-move diagnosis (s30036569 T3):** Forest#10 + Wild Growth, Forest#11 dropped this turn, Arbor Elf + Satyr;
  the line is WG@10 + Sprawl@10 + Overgrowth@10 + Finale X=1 + T&N = 15. Breakpoint holds (outside 1 + stacked 2
  = 3), but Overgrowth-FIRST does not pay (3 + 4 + 6 = 13): with outside mana left, it is better to put the one
  Aura it can buy on the host BEFORE the host's first tap. Aura (Forest#11) -> host taps for 3 -> Overgrowth;
  re-taps 5 + 6, for 1 + 3 + 5 + 6 = 15. General form: at the breakpoint, Overgrowth goes after the first j = min(outside, number of
  same-host one-mana Auras ranked ahead) of those Auras (j = 0 is the current rule). **USER DECISION (not
  implemented; the USER's rule says "Overgrowth first"):** adopt that refinement as a fallback, used when
  Overgrowth-first does not pay. Frequency: 1 board / 51 flags in jumbo-userorder 5k, 0 in jumbo-5o 5k.
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
* **Item 5l (agent 2026-11-11): identical-basic fetch fold: NOTHING TO FOLD, PARKED. USER DECISION below.** The
  fold the USER approved already exists engine-wide: `LandSearchClasses` (core/LandSearch.h) makes copies and
  behavioural twins one class, so "which Island" was never a branch. The sizing doc's claim that Myriad's pins
  "choose which Islands" was wrong. On the 980-plan p3 decision the pick list is `{II}`, `{I}`, `{}`, so the pins
  choose HOW MANY Islands to fetch. That is `land_search_allow_partial`, set deliberately in the deck analysis
  because library size matters on an extra-turn draw engine (deck-out). The "dig variants" are Mind Stone's
  sac-draw no-dig twins (528 of the 980 plans). Taking a draw or keeping the rock is a real-information choice.
  Neither axis is a clairvoyance twin. "Which Myriad" is already folded (`equiv_tag`). Measured on the 11 heavy
  seeds (oracle on): shipped 4:53 wall, 10,265 CAPPED flags, 7 budget-spent decisions; dropping `{}` 4:35 /
  10,144 / 6; no partial picks at all 4:29 / 10,143 / 6. So even the lossy option leaves the gate failing on
  CAPPED-DECISION, and no gate was run. No src change was landed (the probes were reverted). Details:
  `docs/design/extra-turns-mana-oracle-sizing-2026-11-10.md`, "Item 5l result".
  **USER DECISION (5l):** the 2026-11-11 ruling was given on a wrong description. Options: (a) leave Extra Turns
  parked on oracle cost (recommended: no menu narrowing gets under the CAPPED bar); (b) raise the per-decision
  oracle budget for this deck only, or accept CAPPED-DECISION as disclosed for it (a gate-policy call); (c) drop
  Myriad's `{}` pick behind a per-state soundness predicate (no known library top, no land-to-graveyard watcher).
  Option (c) is a sound collapse but small: about 1% CAPPED and 6% wall. Not implemented.

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
  **SUPERSEDED by item 5k below: the mechanism above is wrong, and the generic-debt proposal stays withdrawn.**
* **Item 5k (agent 2026-11-11): gi37 VERIFIED. The hold claim is WRONG; the real cause is a lockstep defect in the
  recorded Lotus Bloom colour, which is FIXED on HEAD.** Repro: hold branch `worktree-agent-afe476c961652c9b8`
  (2b0a3499b), manifests `build/scratch/claude1110/5k/{off,on}_d8_g37.json` (seed 1920038, gi 37, 2HG, d8 b0).
  Instrumentation: `MTG_FD_TRACE=1 MTG_BP_TRACE=1 MTG_SAC_COLOR_TRACE=1` plus a temporary `MTG_5K_FLIP_REPLAY=1` that
  replays the committed line a second time with OPENER_HOLD flipped. It lives only in that worktree, uncommitted.
  Traces are in `build/scratch/claude1110/5k/fl_{off,on}.err` and the condensed `rep_{off,on}.txt`.
  1. At d8 the whole game is committed by the **T1** search (an 8-turn line). Both arms predict a T8 win. Off arm:
     Bloom suspended T4, go-off T7, kill T8 (pred. opp -2). Hold arm: T4 Rite + Desperate + Scourge, Bloom T5,
     go-off T8 (pred. opp -8).
  2. **The off arm's line replayed with the hold ON is identical**: T7 opp 18, T8 opp -2, every payment the same. All
     its casts are red (float B0 throughout T7 until the Bloom's BBB, which the replayed Kolaghan takes). So the hold
     does NOT value the Apex line short. The USER's doubt was correct: Apex's 10 R is plenty.
  3. **The hold arm's line replayed with the hold OFF fails**: T8 opp 6, which is exactly the real game's result.
     Apex #2's {6} generic is paid WUBRG (an opener line keeps the historical order without a hold) out of
     float B3 R13, so the Bloom's BBB is burned and Kolaghan is never cast. With the hold ON the BBB survives:
     Song (B3 R14), then Kolaghan {3}{B}{R} out of B3 R17, so the prediction is -8. The hold therefore makes a
     real line PAYABLE. It does not hide one.
  4. **Why the hold arm still lost: the executor cracked Lotus Bloom for RRR where the search cracked BBB.** Search,
     T8 continuation 0 = {Lotus Bloom crack, Rite, Apex, Karrthus}: `[sac] T8 Lotus Bloom amt=3 -> B demand B1 R5 G1
     (FOLDED)`. Karrthus's {B}{R}{G} is the only {B} demand, and Karrthus then fails to pay (no {G}), so it is not
     recorded. Executor: `[replay-bp] turn=8 recs=3: Lotus Bloom Rite of Flame Apex of Power` -> `[sac] ... -> R
     demand B0 R4` -> float R14 B0 -> `Dragonlord Kolaghan ... B0 R20 -> FAILED`, opp 6. The cause is in the
     breakpoint-continuation pre-pass (`apply_continuation_precasts`, TurnSolver.cpp). It resolves a FOLDED sac's
     colour from the whole continuation's demand but records the action UNPINNED, so the replay re-resolves it
     from the shorter realised list. This is the s4004 gi107 class again, by a different route.
  5. **FIX (HEAD, lossless lockstep):** the recorded copy is pinned to the colour the apply used
     (`chosen_float_color = sac_col`). `SacFloatColorFor` returns a pinned colour unchanged, so the replay realises
     exactly what the search scored. On the hold branch the same pin (env-gated there) turns gi37 into a T8 win
     with the hold ON, at d8 b0 AND at play settings (digest 8bb2b4c115f96634; off arm unchanged). On HEAD gi37 is
     unchanged (T8, e5e1f0e1c450c002), because HEAD has no hold. Validation: units 2828/2828; smoke ALL PASS with 0
     play-changed. Regression: every row is byte-identical
     except extraturns_regression_d3_s2002 gi34 and _d5_s2002 gi62. In those two games the play changed but the
     score did not, and only in the POOLED run: `--deck=extraturns` alone PASSes with BOTH the base and the new
     binary. That is the pooled-run residency nondeterminism (item 5i class), not this change. Held-out A/B
     (`build/scratch/claude1110/5k/ab.json`, seed 7731000+, base PGO c30b25eaf vs new): Dragonstorm Std 2,000 /
     2HG 2,000 / Shadowborn 1,000 paired games, **0 play-changed, 0 faster, 0 slower**. The pin fires only when a
     folded sac's continuation drops a cast whose pips decided the colour. No GT accept was needed.
  6. **For the USER (the hold itself, Q23):** with the pin, gi37 no longer argues against the hold. ab4's other
     slower games (gi605, 3768, 6022, 7784) were not re-checked under the pin. They must be re-measured before the
     hold is reconsidered, and the hold remains unadopted until the USER rules. Point 3 also shows a real
     HEAD-side cost of the historical WUBRG generic order on opener lines: it burns an off-colour float that an
     unseen continuation (Kolaghan's {B}) needs. That is the defect the hold was built to hedge.

* **Item 5i (agent 2026-11-11): plan-cache determinism FIXED (commit "5i: plan-cache determinism").** The
  gods_regression_d3_s2002 drift was NOT residency of a sound cache: the breakpoint continuation cache served
  lists derived under a different CONTEXT. Repro shrank to one game, one thread: gi39 (seed 2041, d3 b10) gives
  b168127794e9caa5 at the default pool and 90830d658bf3fc1e with MTG_PLAN_CACHE_KB=1 or MTG_NO_BP_ENUM_CACHE=1.
  MTG_BP_VERIFY_STATE_SWAP named it: served=299, re-derived on the STORED state = 1, on the hit state = 1, i.e. the
  same state, a different answer. **Leak:** `GodsProvider::LoamTargetSearchCandidates` reads three inputs outside
  its state -- the FULL search root (a Magus of the Bazaar there keeps the 299-entry physical-copy menu), the guard
  root (graveyard land count, Magus) and the mulligan-trial flag (`turncensus::t_probe`) -- and the cache keyed on
  the state only. A second, smaller leak: the Loam target probe (`g_loam_probe_offer`, sentinel "L?" left
  unexpanded inside the probe) was in no key either. **Fix (lossless, keys only):** new
  `DecisionProvider::PlanCacheContextKey` (0 by default; Gods returns exactly the facts its hook reads, through a
  shared `ReadGodsLoamCtx`, hook behaviour byte-identical), folded with the probe bit in `FoldPhysicalCards` (m1/m2
  enum memo + bp cache). Also: `decisionwork::PricedExceeded` now counts a budget READ even with the ceiling
  disarmed, so an entry derived without a ceiling is refused by MTG_CACHE_READ_GUARD where the ceiling has tripped
  (sound: a refused hit recomputes). **The verifier was perturbing play**, which is why the 2026-11-11 WIP fixes
  looked ineffective: MTG_BP_ENUM_VERIFY's re-derivation ran unmuted, advanced `workctr::t_priced`, tripped the
  priced ceiling earlier than play and then reported cut lists it had caused (the verify run played a different
  game: Gods 98a4357d vs 76f2637c). It now re-derives MUTED (exactly play's ledger recompute) and skips hits the
  read guard refuses. **Proof:** Gods d3 s2002 + Extra Turns d3/d5 s2002 give identical digest AND units at the
  default pool, KB=1, KB=65536 and --threads 32/8/4; MTG_BP_ENUM_VERIFY=3 CONTENT-DIFF **0 of 302,384** hits (was
  1,543 on Gods, 18-34 on ET), and the verify run now plays the same games as plain play. Extra Turns gi34/gi62
  could not be reproduced in any isolated configuration before or after (GT digests at every pool size); the ET
  CONTENT-DIFFs were all priced-ceiling cases (verifier-caused or read-guard-refused), so the pooled flake is
  most likely the same priced-ceiling/pool class, now covered by the read fix -- not proven by a reproduction.
  **GT:** smoke moved only gods_smoke_d3_s1001 (play-changed, score 5.36 unchanged); the pre-5i binary with
  MTG_PLAN_CACHE_KB=1 gives the new digest 37dbb911 exactly, so the old GT was the warm-cache artifact. Accepted.
  **Regression (rebased tree bb597608d, Release):** 299 pass, 3 play-changed at same score: gods d3 s2002 gi39
  (-> 76f2637c, the cold-cache digest, explained) and gods d5 s2002 gi39 (-> 9ea56514; same game, same fix, the
  old-binary KB=1 check was NOT yet run for it), plus **extraturns d3 s2002 gi34 -> 639a950d AGAIN** (the exact
  digest the 5k agent saw). A direct pooled rerun of the same manifest/binary (`build/scratch/claude1110/5i/R1`)
  gave the GT digest for ET (gi34 f1d9f642) and the same Gods digests. So the ET flake survives this fix:
  intermittent, pooled-only (2 of 4 full pooled runs), not plan-pool size (single game invariant at
  MTG_PLAN_CACHE_KB 2..16384), not tier-2 (refused=0), bp verifier clean. NOT FIXED -> next suspects are
  thread-reuse state carried between games of DIFFERENT decks on a worker, or a cross-thread shared input
  (GameWorkMeter late_limit / PublishTo). Regression GT NOT accepted (an accept would bake in whichever ET digest
  the run happened to produce); see `logs/mana-clean-chain/5i-handoff.md`.
* **Item 5i CLOSED (agent 2026-11-11 evening, pushed dbd56b648).** Rebased the 9-commit chain onto origin's
  4ac44b926..81a532649. **One mechanism kept:** origin's `DecisionProvider::EnumerationContextKey` (folded by
  BuildBreakpointKey, plus the Loam-probe bit) covers every input our `PlanCacheContextKey` folded (full/guard
  root Magus, guard graveyard land count, t_probe, plus DecisionUnpruned which ours lacked), so ours was dropped
  in the rebase; what 5i adds on top is the PricedExceeded read count and the muted, guard-aware
  MTG_BP_ENUM_VERIFY (commit 712d95f92; b05ad387b's title still says "accept gods smoke GT" but after the rebase
  it carries only docs -- origin had already accepted the cold-cache Gods rows). Small caveat in origin's key:
  under the non-default MTG_GODS_LOAM_SINGLE=7 it reads the full root where the hook reads the
  SolveWithLookahead root (default mode 9 is exact). **Post-rebase:** units 2833/2833; smoke 195 pass / 0
  changed; regression 309 pass / 0 changed (no accept needed; check_gt_logs 1108 consistent); Gods d3/d5 s2002 +
  ET d3 s2002 identical digest AND units at default / KB=1 / KB=65536 / --threads 4, equal to GT. CI
  43073515164 green (Linux + Windows). **Held-out Gods A/B** (pre-5i b1bf4f8a5 vs dbd56b648; d3 s71000 x1000,
  d5 s73000 x1000, Gods2HG d5 s75000 x300): byte-identical, same digests and units -- play-neutral.
  **ET pooled flake (gi34 -> 639a950d): not reproduced on the current tree; PARKED with a diagnosis.**
  Thread history does NOT carry it: --threads 1, gi34 alone / after gi0-33 of its own job / after one game of
  each of the 170 other searched regression jobs -> identical digest f1d9f642 AND units 32,131. Plan pool size
  does not either (KB 1..64M). The only channel left is cross-thread: the plan-cache byte pool is GLOBAL
  (plancache::PoolUsed), so when a store is refused depends on other workers; a residency-dependent unit leak
  shows only pooled. Origin's MTG_MUTE_NO_STORE (4ac44b926, a muted recompute stores nothing) closed one such
  leak and our read-count fix another; both flaky runs (5k, 5i pre-rebase) predate the mute fix. Since the
  rebase, 3 of 3 full pooled regression manifests (the tier run + P1 + P2, build/scratch/claude1111/et) match
  GT on every game of all 309 jobs. If it recurs: rerun with MTG_PLAN_POOL_STATS=1 and compare the refused count
  around ET d3 gi34, then MTG_BP_ENUM_VERIFY=3 on that pooled manifest.
* **Item 5b (agent 2026-11-11): Mirrorwing hedges H1 + H2 BUILT and A/B'd: INERT, PARKED (no src change on the
  branch).** Definitions: followups s8 Q25b (USER 2026-11-06, "build both"). Code kept on
  `wip/mc-mirrorwing-hedges-5b` (94a2e94c0, pushed): levers `MTG_PAY_HEDGE_H1` / `MTG_PAY_HEDGE_H2` (HeuristicArm
  slots, default off) at the per-pip scarcity selection, gated on `g_line_opens_unseen` (the line casts a cantrip),
  re-picking the lowest-rank untapped `usable()` land that makes the pip; plus a count-only audit
  `MTG_PAY_HEDGE_AUDIT` at `TapSourceIntoFloat` (H1/H2 SUPERSETS: any attack-capable creature tapped on a copy-magnet
  pre-combat attack turn / any Treasure cracked, while an untapped land makes that colour).
  **A/B** (one pooled Release batch, base / H1 / H2 / H1+H2, Std 20,000 seed 32000000+gi, 2HG 10,000 seed
  33000000+gi): every arm **byte-identical** to base on all 30,000 paired games (0 digest diffs; Std avg 4.1399,
  2HG 4.4436); the levers **fired 0 times**. Why: in the scarcity key a plain-tier land (key[0]=0) always beats a
  creature (creature band, key[0]=rank>=60), and a land that makes the pip loses to a Treasure only via a reservation
  (`usable()` false), which the hedge correctly does not override. **Audit** (120,000 games, all arms): REAL play
  H1-superset 48 of 29,180 attacker taps, H2-superset 4 of 123,720 Treasure cracks; search applies 0.13% / 0.06%.
  Those come from the other payer paths (whole-turn prepay / backtracker / scripted taps; a hedge-site debug print
  showed the per-pip selection never chose the creature there), and are supersets (no cantrip test), so the real
  rules fire at most ~1 in 2,500 games. Alt land in nearly all: Sandstone Needle (depletion hold / waste band).
  The 11-08 gate's 66 DOMINATED-ONESHOT flags are not H2-shaped: e.g. s30016508 T5 (search apply of Draught + GR +
  GR + Anger) cracks the Treasure only after every land is tapped; the waste is an {R} left floating from Needle's
  {R}{R}, a float-order miss, not a land-vs-Treasure rank. Reopen H1/H2 only on a REAL trace from
  `MTG_PAY_HEDGE_AUDIT=2` (on the WIP branch) whose path is then hedged where it taps.
  **Gate `mirrorwing-5b-20261111`** (PGO, src 385dfc9897a0, block 19000-23999, 5,000 games): **12,272 issues** (11-08
  block 14000: 12,549), play byte-identical oracle off. ENUM-GAP 6,889 (7,128), APPLY-DROP 1,570 (1,301),
  APPLY-DROP-ORDER 99 (183), DOMINATED-CREATURE 227 (198), DOMINATED-ONESHOT 124 (66), ORACLE-GAP 2,924 (3,082),
  UNMODELED 8 (0), CAPPED 431 (591). ENUM-GAP (off-menu) and ORACLE-GAP dominate: Mirrorwing's remaining work is
  menu/oracle coverage, not payer order. Next fresh block 24000.
* **Item 5c (agent 2026-11-11): Niv gi887 known cards -- DONE.** The port itself had ALREADY LANDED at
  5718ccb5b (2026-11-07; followups s8 Q29): `Library::ShuffleUnseen` on main's `m_known_top` mask, called by the
  `MTG_NC_SEARCH` policy (`ReshuffleAvgChoosePlan`), both honest-teacher continuation reshuffles and
  `EmitEvalRows` labels; blind leaf and keep trainer unchanged per the USER ruling. Verified intact on HEAD.
  **Gap closed now:** the mask records only cards the player PLACED; a top shown by a continuous reveal (Courser of
  Kruphix, `LibraryTopVisible`; Landfall) is known but was never placed, so the NC search / labels / blind search
  sampled it away. New `ShuffleUnseenFuture` / `NoteVisibleLibraryTop` (SpellEffects.h) note it on the SAMPLED COPY
  before the reshuffle (all four ShuffleUnseen sites + `blindsearch::BlindInPlace`); the real game's mask is never
  touched. No other engine site reveals without moving (cascade exiles go to the placed bottom block). Unit case
  added; units 2834/2834; scenarios 461/461; smoke 195/195 and regression 309/309 byte-identical (0 play-changed;
  827 references: 0 drift/gap). No GT to accept.
  **Repro** (seed 1650888 = 1650001+887, game_index 887): `MTG_NC_SEARCH=1` d8 b0 wins **T5**, shipped play T5 --
  gi887 passes the bar. Shipped play is byte-identical by construction (only sampled copies change), so no held-out
  A/B and no Niv gate were run for 5c: a gate would re-measure unchanged play (Niv is DUE anyway: 333,811 issues on its last
  5,000-game record, next block 10000-14999; queue item 6).
* **Item 5d (agents 2026-11-11): payer excess-release -- FIXED, three levers, all default ON** (90bd12163,
  30e382818, GT 8806c4c67; local, owner pushes). (A) `MTG_PAY_EXCESS_RELEASE`: the whole-turn prepay give-back
  (`BatchPrepayMainCasts`, MTG_PREPAY_SHRINK) also triggers on MANA over the bill (multi-unit taps), keeping a
  release only as a strict subset of the solve's own taps (Jumbo s30091733/s30090040 DOMINATED-CREATURE 1->0).
  (B) `MTG_PAY_REFINE_C_FOLD` (split off A so each half was A/B'd alone): the per-cast refine reads a floating {C}
  as covered by other surplus of the same pool in a deck with no {C} pip (Angels s30210844 2 DOM-CREATURE + 10
  DOM-LAND -> 0). (D) `MTG_TERA_DRAIN_DOOMED`, found root-causing B's one real loss (Stompy regression s2002 gi412,
  T4->T5 at d8 b0, NOT clairvoyance): the refine gave a Forest back from World War Hulk's bill, Terastodon (free off
  Hulk I) then ate it untapped, and Natural Order had to tap the Priest whose attack made the Hoof swing lethal. The
  destroy loop now taps each untapped basic victim for its one colour first (CR 605.3a; same victims, same order);
  d8 b0 back to T4. A release is a dominance only if the released source lives -- other self-destroy/sac sites
  (Natural Order's sacked dork, sac outlets) are the same shape and were NOT generalised (no repro).
  **Bar:** held-out PGO A/B, seeds 7350001+, play settings, 4 arms (off / A / B+D / all), 15 decks (stompy, angels,
  dragons, giants, mirrorwing, selesnyav2 4000; bruna, elementals, edf, alchemist 1200; niv 4200; fivecolour, jumbo
  400; hinata, ET 200) + post-rebase hinata 400 / martyr 2000 (off vs all): **every deck net <= 0 on every arm**
  (all-on: niv -5, alchemist -2, stompy -1, rest 0; A alone niv -4; B+D niv -2, stompy -1). Work units within
  +-0.5% everywhere (ET A -1.7%). Every slower game recovers at d8 b0 or is clear clairvoyance
  (`MTG_NC_SEARCH=1` d8 b0 equal both arms): niv 94/346 and alchemist 615 clairvoyant; niv 293/1490/1556, ET 86 and
  every tier slower (niv smoke d3 gi9; edf d5 s3003 gi68, ET d3 s2002 gi53, niv d5 s2002 gi70 / s3003 gi21,
  hinata d3 s2002 gi52, martyr d3 s2002 gi28) recover; niv smoke d5 gi46 clairvoyant. ET pooled BD gi83 abandon
  (wt=-1) was the parked ET pooled flake (5i): re-run alone byte-identical to off. Tables:
  `logs/mana-clean-chain/5d/`. Units 2836/2836 (two new cases), scenarios 461/461, viewer protocol 0 drift.
  **Gates (PGO src eb92162cd216):** `angels-5d-20261111` block 215000-219999 **CLEAN, 0 issues** (was 4), oracle-off
  byte-identical; `jumbo-5d-20261111` block 95000-99999 **1450 issues** (was 8036: CAPPED-DECISION 6429 -> 0,
  DOMINATED-CREATURE 284 -> 120, ENUM-GAP 457 -> 502, APPLY-DROP-ORDER 636 -> 592, ORACLE-GAP 192 -> 164),
  oracle-off byte-identical. The CAPPED-DECISION drop is not attributed to 5d (other items landed since that record).
  Still open from the handoff: Angels s30213528 / s30214769 (cross-cast: Giada tapped where Farm W + Sol C across
  casts would do -- line-level, not a per-payment excess) and Jumbo s30093108 / s30094151 (own-host hold paying
  with an Elf unit instead of a fresh Forest) -- PARKED, neither is this mechanism.

* **Item 5f (agent 2026-11-12): Jumbo capped decision -- FIXED (analyzer only, lossless).** Moot check first:
  s30025605 (the sprawlgreen capped seed) no longer caps on HEAD eb92162 (one APPLY-DROP-ORDER, 10 s); the 5p gate's
  seed s30094355 T4 still did (1,099 plans, CAPPED-DECISION 6,429; uncapped it is CLEAN, PHANTOM 199, 10.0M applies,
  9.65M of them in the PHANTOM other-orders loop). Cause: OtherOrders(with_acts) permuted the two equips among the
  casts, but outside the human sequence the explicit route runs every trailing activation after the casts, so the
  5,039 orders of a 7-action plan are 240 applies. Commit 25619b05a: (1) the TRAILING-ACTIVATION ORDER FOLD
  (`MTG_ORACLE_ORDER_FOLD`, default ON; one order per class of cast order x activation order, the first the walk
  meets, so the first hit and its APPLY-DROP-ORDER text are unchanged; `MTG_ORACLE_ORDER_FOLD_CHECK=1` realises every
  folded order and aborts on a difference); (2) the orders loop uses Realize's hit_only (it reads only hits); (3)
  hit-only frontiers below an already-dropped action are not built. s30094355: CAPPED-DECISION 6,429 -> 0, game
  oracle 103 s -> 18 s, verdicts equal the uncapped run. Fold check: 0 mismatches (566k folded orders on the repro;
  50 decks x 6 games). A/B vs HEAD: Jumbo 300 games (95000-95299) identical class for class; 50 decks x 6 differ only
  in 3 budget-spent decisions (EDF, ET: more plans audited, CAPPED-DECISION -80). Units 2836/2836, smoke 195/195
  byte-identical. Gate **`jumbo-5f-20261112`** (PGO, src 124d7bb6f34b, block 100000-104999, 5,000 games): **1,386
  issues**, CAPPED-DECISION 0, play byte-identical oracle off. Per 5k: ENUM-GAP 428, APPLY-DROP 22, APPLY-DROP-ORDER
  631, DOMINATED-CREATURE 118, ORACLE-GAP 185, UNMODELED 2. Next fresh block 105000. LOCAL ONLY, not pushed.
* **Item 5g (agent 2026-11-12): Mirrorwing g662 NC T3 land hold -- DIAGNOSED, PARKED (not a shipped-play defect;
  the instrument cannot answer the question).** Repro on HEAD 01a3adbc1 (seed 73000662 gi662, b20): plain T4 digest
  30bd27827a1bab5e, `MTG_NC_SEARCH=1` T6 digest 4b2558f70c91284e (both = the 11-07 replays; play unchanged since).
  Shipped (clairvoyant) play plays Needle#2 on T3 and wins T4. Mechanism of the NC hold, traced with the new
  `MTG_NC_TRACE=3` (per (plan, sample) win turn, instrument only): T3 `Needle+Zada` avg 5.000 vs `defer+Zada` 4.750
  over K=8; 7 of 8 samples TIE, ONE sample (k=0) gives T6 played vs T4 held. In k=0 the played arm (6 mana on T4)
  has its honest continuation plan `Gold Rush+Anger+Draught` against an IMAGINED library (Anger "draws" Gold Rush);
  against the sample's true order Anger draws Forest + Elvish Mystic, and the turn's payment taps Ignoble Hierarch (an
  attacker that would receive Zada's copies) while a Gold Rush Treasure and an untapped Needle stay unused (the open
  DOMINATED-CREATURE class, 227/5k in the 5b gate) -> 13 damage instead of lethal. The held arm (4 mana) cannot fund
  Draught, keeps Hierarch attacking, kills T4. So the hold is K=8 sampling noise on a one-sample imagined-future
  continuation: `MTG_NC_K=32` and `=64` both play the Needle on T3 (the averaged objective is not structurally
  wrong; the NC tempo bonus is 0 at >= 2 lands).
  **But the NC replay still ends T6 at K=32/64**, and that exposes the real limit: under `MTG_NC_SEARCH` the executor
  makes ONE main-1 plan and never re-decides after a mid-main draw (no breakpoint re-solve fires: checked with an
  entry probe on `resolve_draw_breakpoint`, 0 calls). T4 at K=32: Anger draws Oracle's Restoration + Forest, and
  then nothing -- Gold Rush, Restoration and the Forest drop sit unused with 5 mana up (opp 20 -> 15). The base T4
  kill is exactly such a chain (Anger -> play the drawn Forest -> Restoration -> cast the drawn Heroism/Mystic -> Gold
  Rush), each step a natural choice AFTER seeing the draw. So for a cantrip-chain deck the NC instrument
  under-plays every turn whose kill uses drawn cards, and g662's NC T6 is not evidence of clairvoyance either way.
  Verdict for Q25a stands as "NOT clear clairvoyance" (11-09); the old wip/mc-mirrorwing early-mint arm's T1 land
  change is the separate, draw-independent loss named there. No play change: smoke/regression not run; units n/a
  (trace-only src change, both digests byte-identical). No gate (play unchanged).
  **USER DECISION (raised, not settled):** the slower-game clairvoyance bar uses `MTG_NC_SEARCH=1`; should the NC
  policy get an HONEST mid-main re-decision after each draw (re-run the reshuffle-averaged choice on the post-draw
  state, never the clairvoyant `SolveWithLookahead` the executor's breakpoint re-solve uses), before it is used to
  call Mirrorwing/cantrip games "clear clairvoyance"? Recommendation: yes (instrument-only, default-off path).
  The in-continuation DOMINATED-CREATURE (tap the attacker before a same-turn Treasure) belongs with the open
  Mirrorwing DOMINATED-CREATURE class, not here.

## 2026-11-12: item 6, remaining DUE decks (smallest issue count first)
Pooled gate `due6-20261112` (PGO src fb2903808, 5,000 games each on the next fresh block, play identity with the
oracle off byte-identical; record af059782a). Fresh counts: vampires 0, martyr_lifegain 10 (first run), slivers_vial 32,
creature_giving 242, hydras 249 (first run), shadowborn_apostle 320, pirates 504, treasure_hunt 505, goblins 725,
fungus 1,880, gods 4,300 (first run).
* **vampires CLEAN** (215000-219999, 0 issues; the parked ORACLE-GAP x2 did not recur) -- leaves beta.
* **slivers_vial FIXED (a748c3a16 + c1ed1a922) and CLEAN** on re-gate `slivers-6-20261112` (PGO 51f0ad572,
  215000-219999, 0 issues) -- leaves beta. Two whole-turn prepay bill defects (BatchPrepayMainCasts priced each cast
  with EffectiveCost only): (1) a PINNED Hatchery replicate count's k x printed share was in Action::cost but not in
  the joint bill, so the copy spent the Cavern {C} booked for Aether Vial (s30214758 T3, Galerider x2 + Vial);
  (2) Thrumming Hivepool's affinity credited only Slivers already out, not the line's Slivers ranked before it, so the
  batch read UNPAYABLE and the per-cast greedy stranded Cloudshredder's {R}{W} (s30214131 T4). Levers
  MTG_PREPAY_REPLICATE / MTG_PREPAY_LINE_AFFINITY (heurarm slots, default ON). Units 2841/2841; tiers: only slivers rows
  (slower 0; regression d0 s2002 4.6880->4.6870, d5 s2002 4.2138->4.2123); held-out 6,000 games (7350001+) net 0,
  fast 0 slow 0. Only Hatchery Sliver / Thrumming Hivepool carry the params.
* **martyr_lifegain 10 APPLY-DROP (3 games), PARKED.** s30001947 T3 {Orzhov Basilica + sac Martyr of Sands}: Basilica
  taps {W}{B}, the {1} sac cost spends the {W} (PayFromPool / float generic order W before B) and {B} floats, so the
  breakpoint continuation cannot cast a second Speaker of the Heavens; the menu's {Speaker + sac} plan casts Speaker
  BEFORE the sac and reveals one fewer white card (life 26 vs 29). Both shapes are choices the engine makes without a
  sound dominance (which unit floats; cast-vs-sac order is a cast-order question = USER-owned), so no fix. Repro:
  `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/Release/mtg 'decks/Martyr Lifegain/Martyr Lifegain.cod' --profile
  'decks/Martyr Lifegain/Martyr Lifegain.profile.json' --games 1 --seed 30001947 --game-index 1947`; also s30000359
  (T4, two Martyr casts + sac), s30003252.
* **Triage leads for the rest (not yet worked):** creature_giving ENUM-GAP 232 = double Crop Rotation lines (sac a
  tapped Orchard/land as the extra cost) kept off the menu by a mana gate; hydras 223 APPLY-DROP-ORDER (Search for
  Tomorrow / Nature's Lore vs Nissa / Gauntlet order = cast order, USER-owned); pirates 276 APPLY-DROP mostly "engine
  dropped [cast Lightning Bolt]" after a Staunch Crewmate cast/vial (undeclared later action); treasure_hunt 385
  APPLY-DROP = {Treasure Hunt x2} / {Land's Edge + Treasure Hunt} drops the second Hunt; goblins 608 ORACLE-GAP =
  `sacoutlet:Siege-Gang Commander` paid by Skirk Prospector sac-for-mana, which the oracle does not enumerate ("no
  payment exists") -> ANALYZER-side; shadowborn 260 UNMODELED (long Apostle/Ritual/Culling lines, "too many actions to
  try every order"). Full flag lines: build/scratch/mana_clean/due6-20261112/on.0.err.
* **creature_giving PARKED (2026-11-12, second item-6 agent); WIP on `wip/cg-landtutor-bp` 29f421703 (pushed, NOT on
  the shared branch, NOT validated).** Mechanism of the 232 ENUM-GAPs: Crop Rotation's fetched UNTAPPED land is
  same-phase mana, but every cast is enumerated against the pre-fetch board and no subset gate credits the fetch
  (CheckLine's "Deliberately NOT modelled" note), so "CR, then War-Riders + CR off the fetched Orchard" (s30060041 T2)
  was unreachable at any budget. WIP fix (3 parts): (1) `MTG_BP_LAND_TUTOR` (heurarm): a tutor_land_to_battlefield cast
  arms the deferred site-3 re-solve in both worlds (ParamKeyedDrawClass, the rollout's tutor_land_to_battlefield
  branch, AIEngine creature_tutor_replan, plan-mask bit 3) -- the Jumbo GSZ precedent; (2) oracle: a gate-rejected
  plan whose sub-plan (some hand casts dropped, a land-tutor cast kept) is on the menu is reached by the searched
  continuation (`bp_cont` counter); (3) `MTG_SAC_LAND_DRAIN` (heurarm): an untapped plain-mana sac-land victim taps
  into the float before PerformSacrificeLandCost sacrifices it. On the 121 issue seeds: ENUM-GAP 232 -> 0, but the
  continuation boards now show **DOMINATED-CREATURE 52** (was 3): a continuation Crop Rotation is paid by Birds of
  Paradise while the land that same cast then sacrifices was untapped and could pay (s30063801 T3 {Stomping Ground +
  CR}: engine ends Birds tapped + float {R} from the drained Stomping Ground). SacLandPayVictim (the "victim pays
  first" rule) evidently does not take effect on those continuation casts; MTG_NO_BATCH_PAY=1 does not change it, so
  it is not the whole-turn prepay. NEXT: instrument SacLandPayVictim (front / tapped / returned id) on s30063801 T3 in
  the continuation apply; once DOMINATED-CREATURE is back to ~0, run the full validation (units, smoke, regression,
  held-out A/B on creature_giving + burn (Shard Volley) + any Harrow deck) and the 5,000-game re-gate.
* **hydras: USER DECISION (not settled).** APPLY-DROP-ORDER 223 flags in 69 of 5,000 games (gate due6-20261112):
  the authored HydrasProvider::CastOrderRank (Vernal Bloom 1 < Gauntlet/Nissa 2 < Nature's Lore / Search for
  Tomorrow / Three Visits 3) drops the rank-3 untapped-Forest fetch (Nature's Lore 83+16+12, Search for Tomorrow 46+4,
  Three Visits 39) when the line has TWO doublers or a doubler it cannot pay before the fetch: the paying order puts
  the fetch BEFORE the last doubler, so the fetched Forest pays the doubler (s30000004 T4 {Bloom, Bloom, Search}:
  Search first off two Forests under the first Bloom, then both Blooms; s30000111 T6 {Bloom, Gauntlet, Lore x2}).
  Question: may the fetch move ahead of the last doubler when the ranked order does not pay the line and the moved
  order does (one arrangement per line, the Jumbo MTG_JUMBO_OG_BREAKPOINT shape)? Recommendation: yes, measured by a
  held-out A/B first. Also 19 ENUM-GAP / 6 ORACLE-GAP / 1 APPLY-DROP not yet triaged.
* **Leads gathered for the rest (no src change):** goblins ORACLE-GAP 608 = the oracle has no source face for the
  engine's sac-pay fodder (Skirk Prospector eats Goblin bodies during payment, ManaPayment best_kind 5 /
  CommitPaySacSacrifices; ApplyPreTapImpl has no such PreTap) -> analyzer-only fix, no tiers needed. pirates UNMODELED
  174 = Fiery Islet mana not driven by the oracle (analyzer); APPLY-DROP 265 "engine dropped [cast Lightning Bolt]" after
  Staunch Crewmate: the oracle pays Crewmate with Unclaimed Territory's creature-only mana and keeps Spirebluff Canal
  for the continuation's Bolt, the engine spends the Canal (s30049037 T3). Bolt is cast by the site-10 continuation
  after Crewmate's ETB dig, so the payer cannot see it ("a lost action was not declared in the plan"); keeping the
  Canal is not a deck-wide dominance (Territory's any-colour Pirate mana pays Dire Fleet Captain's {B}), so a fix needs
  the payer to price the HAND's castables (the prepay's hand_demand) -- a payment-policy question, not a prune. shadowborn UNMODELED 260 = 9-10 action
  plans (T1 Thrumming Stone + 2 Apostles + 2 Rituals + Diabolic Intent) the oracle cannot try every order of, so it
  cannot say PHANTOM -> a sound order-free mana upper bound would settle them (analyzer). treasure_hunt APPLY-DROP 385:
  {Treasure Hunt x2} 122, {Cascade Bluffs + Land's Edge + Hunt} 64, {Bluffs + Hunt x2} 45 (lead in 6-handoff.md).
* **Round 3 (third item-6 agent, 2026-11-12).**
  * **goblins PARKED; WIP `wip/oracle-sacpay-fodder` 9449bf6de (pushed, NOT on the shared branch).** Oracle-only:
    `PreTap::sac_fodder` + fodder classes in BuildSources (armed for the frontier payment table only, honouring the
    payer's fodder_ok spares: the cast's sac victim, the activation's source / baked victim captured at the frontier as
    `Hook::f_sac_spare`). On the 288 goblins issue seeds: ORACLE-GAP 608 -> 21, BUT ENUM-GAP 7 -> 791 and CAPPED
    0 -> 727. The ENUM-GAPs are REAL engine menu-gate gaps the old instrument could not see: the gate does not credit
    fodder the line itself creates -- Siege-Gang's three ETB tokens (s30110041 T4 {Siege-Gang + Chainwhirler}: pay
    Siege-Gang eating Warchief, Chainwhirler eating the tokens + Prospector) and Prospectors the line casts (504 of
    791, e.g. s30110142 T1 {Prospector, Prospector, Lackey} each paid by eating the previous Prospector; likely
    dominated but no SOUND dominance proves it). CAPPED is the payment table on wide token boards (5 decisions hold
    ~570). NEXT: (1) an ENGINE change -- the menu's mana gate credits line-created fodder (the MTG_SAC_PAY_PLAN_FODDER
    shape, extended to ETB tokens and cast bodies), a play change needing tiers + held-out A/B; (2) an oracle fold for
    the fodder classes (the payer eats fodder last and as few bodies as possible -- not by itself a dominance, so it
    must be argued); then re-gate. Utopia Mycon decks (fungus) will move with this instrument too.
  * **pirates: ENGINE BUG FIXED (local commit, see git log "Sac-draw lands: re-find the source").** The 174 UNMODELED
    "a mana choice the oracle does not drive: Fiery Islet" were a real defect: the sac-draw activation erased
    `begin()+idx` after a payment that cracked a Treasure sitting ahead of the Islet, so it sacrificed the NEXT permanent
    (s30049175 T4: Daring Buccaneer died, the tapped Islet stayed). Fixed at AIEngine::PerformDigFull and both
    TurnSolver sites; unit test test_sacdraw_refind.cpp.
  * **pirates USER DECISION (payment policy, not settled):** APPLY-DROP 265 of 276 = "engine dropped [cast Lightning
    Bolt]" in 60 of 5,000 games (1.2%); 226 of the 265 after a Staunch Crewmate cast/vial. The per-cast payer pays the
    Pirate with Spirebluff Canal and strands Bolt; paying it with Unclaimed Territory's Pirate-only mana keeps the Canal
    for the continuation's Bolt (s30049037 T3). Question: should the payer, paying a Pirate creature spell, spend
    Unclaimed Territory's creature-type mana before a dual whose colour a noncreature card in HAND needs (Lightning Bolt
    {R}), when no other hand Pirate needs Territory's colour? Recommendation: yes, as a hand-demand-priced preference in
    the per-cast payer (the prepay's hand_demand), measured by a held-out A/B first.

* **Round 4 (fourth item-6 agent, 2026-11-12).**
  * **pirates: sac-draw fix VALIDATED + re-gated.** 91d60f424: smoke/regression only pirates rows, digest-only (searched
    play-changed smoke d5 gi69 / regression d5 gi137 = like-for-like line changes, the Islet sac no longer kills a
    creature); held-out A/B (seeds 7350001+, off = 5db96fbef binary, on = 91d60f424) pirates 5,000 net 0, pirates2hg
    1,000 net 0, elementals / midrangeburn / treasure_hunt 1,000 byte-identical, slower 0. GT accepted de997635d.
    Re-gate `pirates-6r-20261112` (PGO 8fe7c47af, 54000-58999): **263 issues** (fddce2636), UNMODELED 174 -> 0. Left:
    244 "engine dropped [cast Lightning Bolt]" (the Territory-vs-Canal USER DECISION above, not settled) + 15 Dire
    Fleet Captain + 4 Daring Buccaneer drops. Lead for the 19: s30054695 T4 {Spirebluff Canal + Dire Fleet Captain +
    Staunch Crewmate} -- Crewmate's ETB dig draws Goblin Tomb Raider and the site-10 continuation casts it off the
    Mountain the base plan's Dire Fleet Captain needed (the same continuation-vs-base-plan shape as the Bolt class).
    The game still wins T4.
  * **treasure_hunt: FIXED IN PART (50cfe59a2 + d0921c46b), rest PARKED.** `MTG_LINE_SPARE_DEPLETION` (heurarm, default
    ON): LineSpareSourceMask's plain spare set excluded depletion lands, so {Treasure Hunt x2} off Saprazzan Skerry +
    Sandstone Needle paid Hunt 1's {1} with the Skerry's second {U} and dropped Hunt 2 at every budget. A depletion
    tier now taps the otherwise-spare depletion land for the generic, only when no plain spare can be tapped, no
    colour is strictly short, and an exactly-owed colour has a unit in this payment's float. (The first cut, without
    the float/strict guard, was net +2 on Niv over 6,000 games; with it net 0.) Issue seeds (190 games): APPLY-DROP
    385 -> 218, no ISSUE class up. Held-out A/B (Release, flag arm pooled, 7350001+): treasure_hunt 3,000 net -1,
    niv 6,000 net 0, mirrorwing 3,000 net 0, nine other depletion-land decks byte-identical; slower 0. Units 2843.
    **PARKED remainder (218 APPLY-DROP + 30 ORDER + 19 ENUM-GAP + 71 ORACLE-GAP):** filter-land payments.
    (a) s30061353 T4 {Cascade Bluffs + Hunt x2} off Reliquary Tower + Bluffs + Forgotten Cave: the paying route chains
    Cave {R} -> Bluffs#2 {U}{U} -> Bluffs#4 {U}{U} for Hunt 1 so the Tower's {C} pays Hunt 2's {1}; the payer feeds
    one Bluffs and leaves the Tower's {C}, which cannot feed the second Bluffs' {U/R}. (b) s30061127 T5 {Reliquary
    Tower + Land's Edge + Hunt}: Land's Edge {1}{R}{R} must take the Tower {C} for its {1} and one U source as the
    Bluffs feed, keeping Temple of Epiphany's {U} for Hunt; the engine spends both U sources. Both need a filter-feed-
    aware line hold in the shared payer (keep a {U/R} feed / prefer {C} for generic when the line owes U), not
    attempted. ORACLE-GAP 68 = {land + Land's Edge} (s30060179 T4: the engine taps Reliquary Tower, the oracle's
    outcomes do not), analyzer side.

* **Round 5 (fifth item-6 agent, 2026-11-12).**
  * **Integration push** (owner-authorised, once): the 14 item-6 commits rebased onto origin fe0152699 (Hydras flat-gate
    cut, SEL_GATE_SELF_EXCL ON, Hydras keep table); conflicts only the HeuristicArm.h slot list (both kept) and the GT
    header. Smoke 195/195 + regression 309/309 PASS with 0 configs changed; GT re-accepted 0ec21a795, pushed 09:46 UTC.
  * **shadowborn_apostle: FIXED (analyzer, lossless) -- UNMODELED 260 -> 0.** 30bf98ef4: a PHANTOM order walk past
    kPhantomOrderBound (5,040 orders) now searches the plan's trailing-activation FOLD CLASSES (positional multiset
    orders x trailing multiset orders, one representative each, original class skipped) when they fit
    kPhantomClassBound (40,320). Only reached where the full walk stops short, so every completed verdict is
    untouched. Verified: `MTG_ORACLE_WALK_BOUND_TEST=1` (diagnostic: forces 1,501 plans the full walk completes through
    the class walk) gives verdict lines byte-identical to the default run on the 18 issue games, including the 13
    APPLY-DROP-ORDER orders. Unit test updated 276dc6ea8 (8 distinct casts now PHANTOM; 9 stay UNMODELED). The
    per-step affordability bound was NOT needed. Re-gate `th-sb-6r5-20261112` (PGO b28d972b5, 45000-49999): **33
    issues** (was 320): APPLY-DROP-ORDER 14 = Lake of the Dead + Thrumming Stone + Sire of Insanity lines where the
    order Stone > Ritual > ... > Sire pays (cast order; USER-owned, not settled), APPLY-DROP 12, ENUM-GAP 7 (not triaged).
  * **treasure_hunt re-gate** (same run, 65000-69999): **519 issues** -- ENUM-GAP 30, APPLY-DROP 341, ORDER 25,
    ORACLE-GAP 123. **ORACLE-GAP FIXED (analyzer) c0ba6df13** `MTG_ORACLE_FED_ORDER` (default ON): PayEnum::Build
    tapped fed faces in battlefield order, so a chain (Reliquary Tower {C} -> Ferrous Lake {U}{R} -> its {U} feeds
    Cascade Bluffs {R}{R}) was never verified in the order that works; when the default order fails and >= 2 taps are
    fed, every other fed order is paid for real. Issue games (112): ORACLE-GAP 123 -> 0, every other class unchanged,
    digests identical on/off. Re-gated `th-6r5b-20261112` (70000-74999): **375 issues**, ORACLE-GAP 0 (ENUM-GAP 23, APPLY-DROP 299, ORDER 53). **APPLY-DROP 341 still PARKED**: top
    signature {Cascade Bluffs + Land's Edge + Hunt} x24 -- the per-cast payer pays Land's Edge without leaving a
    filter feed / the Tower {C} for Hunt (depth-1 rollback pays; needs a filter-feed-aware line hold in the engine payer,
    full validation; not attempted this round).
  * pirates continuation drops (19) not started.

* **Round 6 (sixth item-6 agent, 2026-11-12).**
  * **treasure_hunt APPLY-DROP: FIXED IN PART (88401100b, GT bd1a8f9d4).** `MTG_LINE_SPARE_FILTER_FEED` (heurarm, default
    ON): LineSpareSourceMask's over-count credited an untapped Cascade Bluffs with two {U} even when its only feed was
    the one floating unit the generic pip was about to eat (s30070121 T4: Land's Edge's {R}{R} off Skerry {U}{U} ->
    Bluffs, its {1} ate the second {U}, the new Bluffs was unfed and Hunt's {U} unpayable). The SHORT test now also
    reads the supply left after the eat (the unit and the starved filter's credit), so the spare Tower {C} pays the {1}.
    Issue seeds (136 games of th-6r5b): APPLY-DROP 299 -> 262, no other class moved. Held-out (Release, flag arm,
    7350001+): th 4,000 / niv 4,000 / cyclone 2,000 / hinata 2,000 net 0 each, 1 digest diff, slower 0. Smoke +
    regression: only th/niv rows, searched slower 0 (th reg d5 gi467 T5 like-for-like: the fixed shape), d0 faster.
    **Tried and DELETED:** a generic-pip reroute to a starved filter's {C} (keep the dual up when it is the filter's
    last feed). Issue seeds 262 -> 213, but held-out th net +1 / cyclone net +1, and th 7353426 + cyclone gi1587 stay
    slower at d8 b0: a filter that is starved at payment time is re-fed later in the turn (a dig reveals a land, a
    ramp filter's output), so keeping the dual up is not a dominance.
    **PARKED remainder (~262):** the per-pip greedy pays Land's Edge's {R}{R} from two duals where one dual fed into
    a Bluffs makes both (s30071552 / s30073118 / s30072175), stranding the filters; and {no land + Hunt} 68 is the
    ORDER/continuation class. The whole-turn prepay (the joint solve that would pay these) DECLINES on every flood-engine
    line (DRAW-SAFE, TurnSolver BatchPrepayMainCasts: a Treasure Hunt in `acts`). The source-level fix named there --
    fold the recorded breakpoint_casts into the joint bill so the prepay can run on Hunt lines -- is the lead; a
    per-pip "route the first pip through the filter" rule was measured wrong twice already (ManaPayment.cpp best_f
    comment: th d0 gi442, Fluctuator s10).
    Re-gate `th-6r6-20261112` (PGO 6858f5eb, 75000-79999): **267 issues** (was 375): APPLY-DROP 234, ORDER 15,
    ENUM-GAP 18, ORACLE-GAP 0, UNMODELED 0; oracle-off play byte-identical.
  * **pirates 19 (triaged, not fixed; handed off).** All 15 Dire Fleet Captain drops are ONE decision (s30054695 T4,
    plan variants of {Canal + Captain + Staunch Crewmate}); not the prepay (MTG_NO_BATCH_PAY=1 identical). The engine
    spends the Treasure -- the only {B} source -- on Crewmate's {1}{U} while two {R} sources stay up, so Captain's {B}
    is unpayable: a payer generic-pip choice for a pay-sac Treasure (LineSpareSourceMask excludes pay-sac sources). The
    4 Daring Buccaneer drops are APPLY-DROP-ORDER (vial Corsair Captain first makes the Treasure).
  * **fungus / gods (triage leads only).** fungus DOMINATED-CREATURE 1089: the oracle pays with Saproling Burst tokens
    shown 0/0 (P/T = fade counters) and calls Shroofus + a 1/1 the attackers -- check the oracle's resource key reads
    the tokens' real power before touching the engine. gods ENUM-GAP 1756: cheap casts off a lone Tendo Ice Bridge with
    its charge counter kept off the menu by a mana gate; CAPPED-DECISION 2330 is one decision (s30002725 T6).
* **Round 7 (seventh item-6 agent, 2026-11-12).**
  * **pirates: FIXED (e0b9de5f7, GT 9af4ce18d).** (a) `MTG_PAY_SAC_LINE_RESERVE` (heurarm, ON): the pain band
    (MTG_PAY_PAIN_LAST) ranked Fiery Islet's {U} behind the Treasure (rank 26), so Crewmate's {U} -- NOT its generic, as
    triaged -- spent the only {B}; a pay-sac one-shot that is the line's last source of an owed colour now taps after
    every other land (the CCO line reserve's band). (b) `MTG_OPS_FIXED_REVEAL` (heurarm, ON): the 4 Buccaneer drops were
    NOT a cast-order question -- the payable-order Vial walk priced the reveal surcharge on the subset's realised order
    instead of the asked one and took "vial Corsair, Malcolm, Buccaneer" ({2}{R}, no Pirate left); OrderPaysSequentially
    now charges the asked order (the USER's 2026-09-27 Corsair > Buccaneer > Malcolm rescue is what it now finds). All
    four seeds clean under MTG_MANA_ORACLE=3. Held-out 7360001+: pirates 5000 net 0 (5 digest diffs), mirrorwing 3000
    net 0. Regression: only pirates d0 digests moved (accepted). extraturns_regression_d5_s2002 gi66 moved digest in the
    POOLED run only; standalone the job reproduces GT on both binaries -> pool-context flake, not accepted, flag for the
    owner.
  * **gods ENUM-GAP: FIXED (e0b9de5f7), analyzer-side + Kruphix.** Not a menu gate: HumanPreTapOfferFor's plain faces read
    the DEFINITION (EffectiveProduces), so a counterless Tendo Ice Bridge offered any colour and the oracle "paid"
    Looting's {R} off it (s30004862: T1 Looting spent the counter; the engine was right). Now per-permanent
    (EffectiveProducesFor); also removes phantom Kruphix bank colours. Gods 3000 held-out vs old binary net -1.
  * **fungus DOMINATED-CREATURE: real engine miss, FIXED 72c31a8cc (`MTG_PAY_DOOMED_BODY_FIRST`; held-out fungus 4000 net 0, 0 diffs; regression + gate owed).** The 0/0 names are
    printed P/T only; the oracle is right: s30050019 T4 the plan's own last fade activation kills the five Burst tokens,
    and the payer (creature band, battlefield-order tie; autonomous hold value is 0) tapped Shroofus + a lasting 1/1
    instead of them. PlanTraits::fade_exhaust_src + creature-band key[1] = -1 for a doomed token. 548 of 1089 flags carry
    a Burst activation; the rest (e.g. s30050039 T3 Ancient Tomb + Hickory Woodlot float) are other shapes, untriaged.
* **Round 8 (eighth item-6 agent, 2026-11-12).** Rebased onto origin (7723fa1b3, then 9caf3af8c): smoke 195/195 x2 and
  regression 309/309 with 0 configs changed (covers 72c31a8cc's owed regression; the Extra Turns pooled flake did NOT
  recur), check_gt_logs consistent, nothing to accept. Pushed **9ba5851a1** 15:36:43 UTC; CI 43249679575 green.
  Gate **pgf-8-20261112** (PGO 26198d90, one pooled run):
  * **pirates 246** (59000-63999; was 263): APPLY-DROP 243 `cast Lightning Bolt` (the USER-held Territory-vs-Canal
    payment policy, unchanged) + 3 Siren Stormtamer (rescued by MTG_PAY_ROLLBACK). The Dire Fleet Captain 15 and Buccaneer
    4 are gone (e0b9de5f7 confirmed). Nothing else.
  * **gods 10350** (5000-9999; was 4300): ENUM-GAP 1756 -> **86** (the per-permanent pre-tap fix), APPLY-DROP 14, ORACLE-GAP
    22, CAPPED 65, and **CAPPED-DECISION 10163 from only TWO decisions** (s30007073 gi3 T6 3974 lines, s30009146 gi6 T4 1026
    lines): Tendo Ice Bridge + Looting/Loam/retrace/Careful Study menus -- the same analyzer decision-budget shape as
    s30002725 last gate. Analyzer perf, not an engine miss.
  * **fungus 1169** (55000-59999; was 1880): DOMINATED-CREATURE 1089 -> **643**, ENUM-GAP 179, CAPPED 267, ORACLE-GAP 69,
    UNMODELED 10, APPLY-DROP-ORDER 1.
  * **Triage leads (not fixed).** fungus DOMINATED-CREATURE 643 over 200 games: 428 keep a lasting 1/1 Saproling (Brightcap
    Badger's granted {T}: Add {G}) and 388 of those have the oracle pay with **Ancient Tomb**. Shape (s30055300 T3,
    `{land=Forest + cast:Sporecrown Thallid + cast:Brightcap Badger}`; s30055011 T3 `{Forest + Saproling Migration +
    Brightcap Badger}`): the oracle pays the first cast with Woodlot (+Forest), floats the extra, and pays the second with
    Ancient Tomb's {C}{C} + the float -- exact, Saproling untapped. The engine's apply taps the Saproling AND Forest AND Tomb
    (6 units for a 5-unit line, `float={*x1}` at the end). Tap trace (temporary `TapDbgReal` override, not committed):
    `Woodlot; granted {G}; Forest; Tomb` -- the Saproling is tapped while Tomb is still untapped, i.e. inside one payment
    the creature band beat Tomb for a generic/G pip, then Tomb paid a single remaining pip and floated one. Lead: find
    why Tomb (tap_self_damage_any_mode, yield 2) loses to a creature there (its kind/band for a generic pip, or the
    harm/usable division at ManaPayment.cpp ~2585 when only one unit of this payment is left but the LINE owes more:
    `payable` reads only this payment's nd_left, not g_line_unpaid_cost -- the depletion-waste rule at ~2833 already adds
    the line's later demand). Repros: `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=3 build/PGO/mtg decks/Fungus/Fungus.cod
    --profile decks/Fungus/Fungus.profile.json --games 1 --seed 30055300 --game-index 55300` (and 30055011/55011).
    A second fungus shape (s30055537 T4, plan with `act:Saproling Burst(x1)` that empties the Burst): doomed 0/0 tokens
    should pay ahead of LANDS too (Tomb/Forest kept by the oracle), but MTG_PAY_DOOMED_BODY_FIRST only reorders inside the
    creature band (key[0] = rank); moving a doomed body ahead of lands needs the line's colour reserve respected (a doomed
    {G} may be the line's last G) -- not a one-line change.
    gods CAPPED-DECISION: analyzer decision budget on Tendo + Looting/Loam/retrace/Careful Study menus (2 decisions per
    5000; 3 so far across gates: s30002725 gi60 T6, s30007073 gi3 T6, s30009146 gi6 T4) -- PARKED as analyzer perf.
    gods ENUM-GAP 86 (19 games): `{no land [ordered] + Careful Study|Faithless Looting + Life from the Loam}` where the
    oracle's pay2 taps a Tendo Ice Bridge that is in HAND on the plan's board (s30005620 gi22 T8: board Tendo#55 + City of
    Brass, pay2 uses Tendo#56) -- check whether the oracle realises an undeclared land drop for a "no land" plan (oracle
    false positive) before touching the engine's menu gate.

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
