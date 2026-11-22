# heath handoff 3 (2026-11-22 ~12:40 UTC) -- for a Codex/Claude agent with NO context

Read CLAUDE.md first (box lock, lane, never push phase-1-2-deck-analyzer, references commit-only, no `timeout`).

## Trees / branches
* `.claude/worktrees/heath-produces` = branch `wip/heath-produces` @ 088ecfbc6 (pushed). Round 2: Heath+Foothills static
  `produces` [W,B,R,G], Mesa/Strand [], Orchard-before-fetch (`MTG_ORCHARD_BEFORE_FETCH`, default ON). Binary
  `build/scratch/heath/mtg.final`. DO NOT edit it while r3 `clair_chain.sh` (below) has not finished (its final arm reads its cards.json).
* `.claude/worktrees/heath-base` = detached 42d819bc0 (committed tree, the A/B base). Binary `build/scratch/heath/mtg.base`.
* `.claude/worktrees/heath-derived` = branch `wip/heath-derived` (from 088ecfbc6) = the NEW work: deck-derived fetch hand hint.
  Binary (after build) `build/scratch/heath/mtg.derived`.
* Scratch: `/workspaces/MagicDeckTester/build/scratch/heath/{r3,r4}`; tools `ab_report.py` (dir with wins_base/wins_new
  symlinks), `tools/gdiff.py`, `d8_manifest.py <manifest> <slowerfile> <out>`, `r3/d8_arm.sh <base|final> <manifest> <name>`.

## Results so far
1. d8 b0 on the 10 r2 slower play games (logs/heath/slower_play_r2.txt): ALL 10 recover (final d8b0 == base A/B turn; 8/10
   byte-identical digests across arms; none abandoned). Outputs r3/d8_base, r3/d8_final.
2. Burn/Morph/Sea larger held-out play A/B (seed base 2000000, b20 d5; r3/wins_pbase vs r3/wins_pfinal):
   Burn 30000 g: 15 changed, 6 faster / 9 slower, net +4. Morph 30000: 26 changed, 13/12 + 1 lost (gi4284 7->unwon), net +3.
   Sea 10000: 13 changed, 2 faster / 11 slower, net +9. CPU final/base 0.99. Every changed game's first divergence is the
   T1 fetchland played earlier (r3/changed.txt; single-game replays with logs in r3/games/, all reproduce).
   d8 b0 on all 33 new slower/lost games (r3/d8b_*): 33/33 recover (final == base turn), so these are budget effects.
   Sea 2:11 sign (p~0.02) -> decoupled-clairvoyance test queued (item 3).
3. DONE (13:50 UTC) decoupled A/B, r3/crep: with the search's shuffle decoupled from the real one (shuffle_salt_search 1):
   Burn 30000 net 0 (12 faster / 12 slower), Morph 30000 net -22 (26/7), Sea 10000 net +5 (13/16, 1 lost / 1 gained);
   Sea blind search (blind_salt 1) 10000: 172 faster / 144 slower, net -34 on both-won, 30 lost / 22 gained.
   READING: Sea's 2:11 (r3 lockstep) becomes 13:16 once the search cannot see the real reshuffle, so the round-2 Sea / Burn
   deficit is fetch-reshuffle timing under the lockstep (clairvoyant) search, not a play mechanism; Morph turns clearly
   negative. CPU final/base 0.95-0.96 on every key. (was: RUNNING: r3/clair_chain.sh (manifest r3/manifest_clair.json: Burn/Morph/Sea play with `shuffle_salt_search: 1`, plus Sea
   with `blind_salt: 1`). Base arm DONE (r3/wins_cpbase); final arm waiting on the box lock -> r3/wins_cpfinal, marker
   r3/DONE_clair, log r3/progress_c. Report: `mkdir r3/crep; ln -s ../wins_cpbase r3/crep/wins_base; ln -s ../wins_cpfinal
   r3/crep/wins_new; python3 ab_report.py r3/crep`. Reading: if Sea's +9 vanishes when the search's shuffle is decoupled /
   blind, the deficit is the clairvoyant "crack-when-the-reshuffle-helps" option value that holding the Heath gives base.

## NEW WORK: deck-derived fetch hint (wip/heath-derived, committed; build+units pending)
USER rulings (verbatim):
* "I don't know what this is for, but W/U is understating the power of fetchlands. They can get any shock in the deck, so
  they can theoritically produce any colour those can get."
* "Yes, for 1 we should count shocks. I'm less certain about triomes because of the come-into-play-tapped, but usually
  shocks cover the colours we need. 2. Orchard first makes sense for the decks you mentioned."  -> Orchard-before-fetch is
  USER-APPROVED for Creature Giving + Stuffy Doll (default ON; record in doc, drop from open questions).
* "I'm more concerned about areas that help searched." -> searched shapes >= 1000-2000 games; report searched FIRST, then d0,
  then CPU; d0 must still pass the every-deck bar.
* Bar: every deck net <= 0 vs base (searched AND d0; "+1 is not neutral, 0 is neutral"); every slower game recovers at
  --depth 8 --budget-ms 0 (escalate budget if it cannot settle); only CLEAR clairvoyance (MTG_NC_SEARCH=1) may stay unrecovered.
  If a deck fails: root-cause + report; do NOT drop the change on your own. Never push phase-1-2-deck-analyzer.
Implementation (src/core/SpellEffects.h, before EffectiveProduces): `FetchReachColourMask` / `HandFixingProduces(state,
controller, def)` = union of colours of LIBRARY lands whose subtypes match `fetch_land_types` and that are not
`enters_tapped` (report-only per-job lever `MTG_FETCH_HINT_TAPPED`, heurarm slot FETCH_HINT_TAPPED, counts tapped too).
Wired into: `EffectiveProduces(..., in_hand=true)` (8 hand readers), LandPlay.cpp greedy multi-colour pass (2 sites),
TurnSolver.cpp SimulateLandPlayOne mirror (5 sites ~84127-84194), Creature Giving cleanup cover (DecisionProviders ~23474).
Static `produces` in cards.json is UNCHANGED and stays as the fallback for the ~60 unaudited direct `params.produces` readers
(mostly per-deck provider heuristics; the fetch-deck FetchCandidates sites already skip fetchlands or read targets) -- say so
to the USER. Unit test added in test/unit/test_boars_cards.cpp. Derived reach per deck: only Five Colour differs with tapped
lands (W via triome/Jetmir); Mesa -> RW in Burn/Stuffy, Strand -> GU Sea / BUW Spirits / RU Worldfire (watch Stuffy/Burn d0
and Worldfire: round 1 saw Mesa [W,R] Stuffy +40/Burn +26..68 d0, Strand Worldfire +60/+17).

RUNNING:
* r3/build_derived.sh (lane, 2 slots): builds heath-derived, copies mtg.derived, runs units -> r4/build.log, r4/unit.log,
  marker r4/DONE_build.
* r4/chain.sh (waits for DONE_build, then box lock): base arm (mtg.base @ heath-base, r4/manifest_base.json) then derived arm
  (mtg.derived @ heath-derived, r4/manifest_derived.json = same jobs + `fivecolourtap_*` jobs with flags
  MTG_FETCH_HINT_TAPPED). 14 shapes x d0 100k (seed 7000000) + play (seed 3000000; AL 8000, AL2HG 4000, Boars 6000 b40, CG 6000
  b30, FC 3000 b40, Mill 1000, Minotaur 4000, Burn 10000, Morph 10000, Sea 4000, Spirits 4000, Stuffy 2000, Cyclone 2000,
  Worldfire 2000; b20 unless noted). Outputs r4/wins_base, r4/wins_derived; marker r4/DONE_ab.
  Report: `mkdir r4/rep; ln -s ../wins_base r4/rep/wins_base; ln -s ../wins_derived r4/rep/wins_new; python3 ../ab_report.py
  r4/rep r4/rep/detail.txt` (fivecolourtap_* keys compare against nothing in base: compare them to fivecolour_* in derived by
  symlinking wins_new->wins_derived and wins_base->a dir of renamed fivecolour files). CPU: sum `cpu=` per key in r4/*.out.

## Next, in order
1. When DONE_clair: report decoupled numbers (item 3) into the doc.
2. DONE_build rc=0 (14:52). First unit run: 2 failures, both tests with an EMPTY library (Mill fetch proxy, CG C6), where the
   library-derived hint is correctly empty; tests now get a library (committed). Rerun: r4/unit2.sh -> r4/unit2.log,
   marker r4/DONE_unit2 (expect 3877/3877). r4/chain.sh started (base arm waiting on the box lock since 14:52).
3. When DONE_ab: searched per deck FIRST, then d0, then CPU; d8 b0 every slower searched game (d8_manifest.py with
   r4/manifest_derived.json + detail file; run base arm with `r3/d8_arm.sh`-style script pointing at mtg.base/heath-base and
   derived at mtg.derived/heath-derived, lane 2 slots each). Root-cause any deck > 0 (first-divergence summary: see r3 python
   one-liner pattern in this handoff's predecessor work: compare action lists of r3/games/<case>_<gi>/{base,new}/*.json).
4. Update docs/design/heath-produces-2026-11-22.md (in heath-derived) with rounds 2-4 + USER rulings.
5. Rebase heath-derived onto origin/phase-1-2-deck-analyzer (rebase only), ./build.sh, units, `MTG_PGO=0 test/regression.sh
   --both` under `flock .../claude_box.lock env MTG_RSS_CAP_GB=40`, accept smoke+regression with
   --accept-with-regressions="<attribution>", `python3 test/check_gt_logs.py`, `python3 scripts/check_references_safe.py`,
   commit, push wip/heath-derived ONLY.
