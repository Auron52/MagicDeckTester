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
* Chain order: build -> unit -> the s30020022 repro -> smoke (expect only Shadowborn rows to move) -> regression ->
  `./build.sh pgo` -> Shadowborn 5,000 gate on block 25000-29999 -> record, commit, push.

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
