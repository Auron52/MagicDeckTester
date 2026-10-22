# Analysis ledger — Vampires

Deck: `decks/Vampires/Vampire.cod` (the user's file name; the folder is `Vampires`). B/R/W Vampire tribal on
Aether Vial: 4 Falkenrath Pit Fighter, 4 Falkenrath Gorger, 4 Legion Lieutenant, 4 Stromkirk Captain,
3 Stromkirk Condemned, 2 Captivating Vampire, 2 Bloodghast, 4 Markov Baron, 2 Asylum Visitor, 3 Vampire of
the Dire Moon, 1 Vampire Socialite, 1 Mavren Fein, 1 Drana, 2 Bloodline Keeper, 4 Aether Vial; lands
4 Voldaren Estate, 4 Unclaimed Territory, 4 Secluded Courtyard, 2 Blackcleave Cliffs, 6 Swamp (20).
Sideboard (Anje Falkenrath, 2 Anje's Ravager, Worldfire, Mountain) is unreachable (no wish) and unscanned.

Analysis started 2026-10-18 in worktree `.claude/worktrees/vampires`, branch `vampires-analysis` (from
origin `phase-1-2-deck-analyzer` 2176032dd).

## Standing decisions (pass to every subagent)
- Goldfish: the opponent never casts spells, never blocks. Model every clause faithfully; a goldfish-inert
  deferral stays PROVISIONAL until the user signs off.
- No greedy pick inside the search window (user hard rule). Every new decision (which card to discard,
  madness cast-or-not, Bloodghast return, which Vampire to sacrifice, Blood/Keeper activations) is SEARCHED;
  a provider may only order/restrict.
- Cast order / range / main-split / mulligan / discard buckets are USER-OWNED: author and present, never
  adopt unilaterally.
- One heavy job (build or batch) at a time.

## Stage 1 — coverage (2026-10-18)
Missing (16): Falkenrath Pit Fighter, Falkenrath Gorger, Legion Lieutenant, Stromkirk Captain, Stromkirk
Condemned, Captivating Vampire, Bloodghast, Voldaren Estate, Swamp, Markov Baron, Asylum Visitor, Vampire of
the Dire Moon, Vampire Socialite, Mavren Fein, Drana, Bloodline Keeper.
Present (4): Unclaimed Territory, Secluded Courtyard (Pirates/Soldiers), Blackcleave Cliffs, Aether Vial.

## Stage 2 — research clusters (fan-out, 2026-10-18)
- A1 madness: Falkenrath Gorger, Markov Baron (convoke, lifelink, lord), Asylum Visitor (upkeep draw).
- A2 discard/sacrifice outlets + recursion: Falkenrath Pit Fighter, Stromkirk Condemned, Voldaren Estate
  (Vampire-only mana, Blood token), Bloodghast.
- B lords: Legion Lieutenant, Stromkirk Captain, Captivating Vampire, Bloodline Keeper // Lord of Lineage,
  Vampire of the Dire Moon, Swamp.
- C combat/ETB triggers: Drana (first-strike-step combat-damage counters), Mavren Fein, Vampire Socialite.
Design files (committed): `docs/design/vampires/design_{A1_madness,A2_outlets,B_lords,C_combat}.md` —
verbatim Scryfall fields, clause tables, full cards.json entries, C++ plan with file:line, search design,
viewer wiring, unit-test plans. Summary of what they established:
- No madness, no Blood token, no Swamp, no first-strike damage step in the engine today. Combat damage is
  one summed step (`ResolveCombatDamage`, shared by executor and rollout); nothing reads first strike.
- The goldfish opponent HAS creatures in 8 of 10 game slots, so Captivating Vampire's steal is LIVE
  (implemented, not deferred).
- Every non-cleanup discard in the engine today takes the provider's first pick (not searched). Vampires
  gets a searched discard pin (one branch per distinct name) and a searched sacrifice victim.
- Secluded Courtyard cannot pay a sac outlet's coloured cost today (the sac-outlet payment skips
  `CreatureAbilityPayScope`) — a mis-model; fixed generally.
- Routing hazards: Markov Baron's `convoke` (Melira Pod signature), Aether Vial (Vial), a sac outlet
  (goblin). The deck gets `VampiresProvider`, routed above all three.

### Integration plan (serial where coupled)
- Base commit: deck file, ledger, designs, Tier-1 cards Swamp / Legion Lieutenant / Vampire of the Dire Moon.
- Track C (own worktree `.claude/worktrees/vampires-c`, parallel): two-step first-strike combat, Stromkirk
  Captain (lord + `grants_first_strike`), Drana, Mavren Fein, Vampire Socialite, second-main detection.
- Track A (this worktree): A1 + A2 — shared discard hook + madness, outlets, Voldaren Estate / Blood,
  Bloodghast, Gorger, Baron (convoke generalised), Asylum Visitor, VampiresProvider + routing.
- Then Track B (this worktree, after A — shares the activation pass and the pinned Action size):
  Captivating Vampire (lord + steal), Bloodline Keeper // Lord of Lineage (token + transform),
  Unclaimed Territory / Secluded Courtyard note lines.
- Then C is rebased in, smoke/regression byte-identity checked, Stage 3 coverage re-run.

## Status (orchestrator, 2026-10-18)
- Stage 2 integrated in order A2 -> C (cherry-picked from `vampires-combat`; cards.json merged per card, no
  card edited on both sides) -> A1 -> B. Unit 1514/1514. Backup: origin `backup/2026-10-18/vampires-analysis`.
- Stage 3: coverage clean (20/20 full, 0 missing); every remaining bracket note is an implementation note or
  a PROVISIONAL goldfish-inert collapse listed below. Cost audit rc 0 (450/450, B integrator). Field audit:
  all hard fields match; its rc 1 is the pre-existing unfetched Temple of Abandon (not this deck).
- Stage 4: baseline profile `decks/Vampires/Vampire.profile.json` (static keep; no keep table / value leaf).
  Cost diagnostic NO_COST_INTERACTIONS. Discard analysis DISCARD_UNLABELLED: 0 real cleanup sheds in 400 d3
  games, 64,621 rollout sheds (all with < 4 lands out, each taking ranking index 0) -> BOUND it with the
  MTG_SHED_WORST pair (Stage 5i).
- Stage 4a: VampiresProvider (derives VialProvider; empty of judgement hooks except the AGENT-DRAFTED orders,
  `MTG_VAMP_DRAFT_ORDERS`) routed at the top of the chain; `provider_audit.py --check` rc 0.
- Stage 4-bis: rows in all three tiers (colossus shape, overnight ENTRY set, vampires2hg in each tier).
  Smoke accepted (46b709eeb) and regression accepted (ff401f887), both FILTERED accepts over FULL-tier runs in
  which every other deck was byte-identical; critter/angels moved with the two-step combat (smoke: 0 slower,
  19 searched + 65 d0 faster; regression: 0 slower, 45 searched + 79 d0 faster). Their OVERNIGHT GT is left
  to the periodic overnight run (CLAUDE.md).
- Reference gate (regression mode): ONE play-drift, CritterLifegain/claude_s6_gi5 (recorded T5, replays T4).
  BISECTED: origin 2176032dd ok (T5); combat track alone T4; combined T4. Mechanism: two Ocelot Prides'
  first-strike lifelink reach 30+ life before regular damage, so Serra Ascendant connects as a 6/6. Reference
  untouched -- open question 27.

## Status update (orchestrator, 2026-10-18 late)
- Claude-play sweep 1 (20 seeds, `docs/design/vampires/claude_sweep_1.md`): 20/20 tie the search; 12 defects
  A-L, all fixed on `vampires-fix` (see "Sweep fixes" above) and cherry-picked here, with the AI-drafted cast
  order + discard buckets from `vampires-order` (DEFAULT OFF; `docs/design/vampires-cast-order-2026-10-18.md`,
  which now answers questions 11-12's "agent drafts" half -- the USER review is pending). Unit 1553/1553.
- The smoke (46b709eeb) and regression (ff401f887) Vampires GT were accepted while defect A (phantom convoke
  mana) was live -> re-accepted after the fixes; the overnight rows were never run on the defective binary.
- The payer fix (F + L: MTG_PAY_PAIN_LAST, MTG_PAY_NARROW_RESTRICTED_FIRST, default ON) moves payments in other
  decks (smoke: 153 non-Vampires games, all digest-only at searched depths; d0: 4 faster, 2 slower soldiers).
  A payment-ORDER heuristic change across decks earns adoption by a held-out A/B on the decks it moves:
  `test/tools/payer_pain_ab.py` (8 decks, both levers on vs =0, two held-out blocks, one pooled batch).

## Status update (orchestrator, 2026-10-18 night)
- Payer A/B (test/tools/payer_pain_ab.py; 8 decks x 2 held-out blocks, one pooled batch): FIRST run failed the
  bar (vampires +1 -- the levers skipped a T1 land on s13480130 gi9 -- and creature_giving +1). Root-caused and
  fixed on `vampires-payer2` (merged: Pit Fighter's main-2 activation holds an ability-legal {R}; a sac-land
  cast's victim pays first). RE-RUN: every deck net <= 0 (vampires -1, creature_giving -3, minotaur -2,
  pirates -2, th -4, auras / selesnya / soldiers 0), ZERO slower searched games; three d0 slower games are the
  d0 runner's later choice after a better payment (th gi1155 decks itself -- question 38, minotaur gi83,
  soldiers gi1554).
- GT re-accepted on the merged tree: smoke 6c5442e74 and regression 5b4b21c98 (filtered accepts over FULL
  runs; every move attributed in the commit messages: searched slower = 2 vampires games, gi76 an old
  phantom-mana line verified on the pre-fix binary, gi79 budget churn that recovers at d3/d5/d8 b0; d0 slower =
  vampires phantom-mana / d0-greedy classes and pirates gi628 payer churn). Overnight vampires rows: running
  (MTG_PGO=0, byte-identical to PGO).
- Remaining: overnight accept; perf track (slow games, `vampires-perf`); leaf tie-break check (5c2); shed
  bound (5i); verify_deck full (mismatch harnesses, viewer sweep, play_invariants); confirmation claude-play
  sweep (incl. a game-index with opponent creatures for Captivating's steal); cast-order / bucket A/B.

## Status update (orchestrator, 2026-10-18 late night)
- Overnight vampires + vampires2hg ENTRY rows accepted (bcbd21481, filtered, 16 new keys; d0 4.70-4.73, d3
  4.41-4.43, d5 4.40-4.43, 2HG d5 4.93-5.05). Left as accepted through the later play changes below: the periodic
  overnight re-accepts (CLAUDE.md).
- Stage 5f search cost (`docs/design/vampires-search-cost.md`; perf commits cherry-picked as f95a154d6 / 40e4951f2 /
  1feccc32b / 2ed53cf74 = the doc's 6161e387c / 46f82d9e5 / ee3dbd8f6 / 1b34af3eb): three folds, default ON --
  `MTG_VAMP_M2_PUMP_DISCARD_FOLD` (dominance, subset rule 9), `MTG_VAMP_MADNESS_COLOR_FOLD` (identity),
  `MTG_VAMP_OUTLET_NOT_GROWTH` (sound). The slowest single-player games cost 2.9-3.7x fewer units (gi131 d5
  4.86M -> 1.44M); held-out 400 games and a d5 b100 sample byte-identical in win turn. 99.7% of a slow game's units
  are mulligan-bottoming trial games, which the keep table removes. Questions 39-42.
- GT re-accepted on the perf-merged tree (89eddb1f6, filtered over FULL runs; every other deck byte-identical):
  smoke searched 0 slower / 1 faster, d0 0 slower; regression searched 0 slower / 3 faster, d0 1 slower (gi265
  4->5, the d0 greedy's extra T3 pump; the perf doc).
- Claude-play confirmation sweep 2 (`docs/design/vampires/claude_sweep_2.md`): 8 Opus players, seeds 92000-92007 at
  game-index 2..9 (opponent creatures, so Captivating Vampire's steal ran its full cycle in three games): 8/8 tie
  the search; every sweep-1 defect class re-checked clean; ONE new defect, M.
- Defect M (fixed, 4493e2158 = vampires-fixm 4a36fa9cd): Voldaren Estate's Blood cost variants counted only the
  plan's Vampire CASTS, so a Bloodline Keeper token tapped earlier in the same plan never discounted the Blood --
  a completeness hole within one main phase at any budget (a pre-combat Blood affordable only at the true cost was
  unfindable; mostly masked by the main-2 Blood reaching the same end-of-turn board). Rule 3 now counts the
  arrivals that run BEFORE the Blood in the shared trailing order (TurnSolver::TrailingRunsBefore, the same keys as
  OrderTrailingActivations); madness casts / steals that run after it are correctly not counted. 0 of 110 paired
  Vampires games moved; KittyEquipment / Colossus byte-identical. Question 43.
- Static checks: verify_deck --no-sweep GATE PASS; whole-repo audit_card_costs 450/450.

## Stage 5 measurements + the 3x COST GATE (orchestrator, 2026-10-19)
One pooled batch on 89eddb1f6 (117,000 games; `build/scratch/vampires/stage5/combined.py`, report `report.txt`):
- **5c2 horizon tie-break: NO SIGN** -- 0 win turns changed of 12,000 paired games at play settings (d5 b20); the
  lever FIRES (play diverged in 3 of 12 blocks), it never binds a win turn (the deck wins T4-5, inside the horizon).
  The default (ON) costs nothing. The script's 48-block re-run was not run (question 44).
- **5i discard-ranking bound:** rollout cleanup best-vs-worst (`MTG_SHED_WORST`) d3 -1 / 3,000 (WORST faster by
  one), d5 0 / 1,500: the ranking is worth nothing at play settings. Non-cleanup discards: `MTG_NONCLEANUP_SHED_WORST`
  inert (0 digests) BY DESIGN -- every Vampires outlet discard (Condemned, Pit Fighter, Blood crack) is a searched
  pin per distinct name, so no ranking decides one.
- **Cast order / buckets A/B** (`docs/design/vampires-cast-order-2026-10-18.md` section 5): the drafted order is
  win-turn NEUTRAL (0 of 16,000 paired games moved); the drafted buckets net -14 (d0) / -2 (d3) / 0 (d5) / -2 / -3
  (2HG d3 / d5), 0 slower searched games, 16 slower d0 games (being root-caused). USER-owned: both stay OFF.

**COST GATE FAILS: `suite_gate.py --cost decks/Vampires` = 7.90x the reference (Galadriel), limit 3x.** Read with
three corrections, none of which changes the verdict:
- That run was CONTENDED (an agent build alongside): its worst row, vampires2hg d5 s2002, cost 23.4 s/game; the
  full regression run on the same binary had it at 10.6 s/game (std d5 3.1-4.6 s, d3 2.1-2.6 s) -- still ~3.6x the
  cached reference. Vampires is 25.8% of the regression tier's core time (18.0% of smoke).
- The cached REFERENCE is stale: `test/suite_cost.json` holds Galadriel at 2,969 ms/game, measured before its keep
  table; the same regression run had Galadriel at 60-94 ms/game. Refreshed, the reference would be the next deck
  with both artifacts (Snow, 1,319 ms/game cached), and Vampires would be ~8x. (Every deck's gate reads that stale
  number; a `--measure-all` refill from one pooled full-tier run fixes it.)
- **The excess is the pre-keep-table mulligan bottoming** (CLAUDE.md: "before the keep table every mulligan game
  plays out each candidate bottom as a full game"). Measured, `MTG_BOTTOM_ROLLOUTS=1` vs `=0`, units/game
  (deterministic): 2HG d5 s2002 248,662 -> 78,570 (3.2x), std d5 s3003 122,925 -> 30,197 (4.1x), d3 s3003 71,502
  -> 17,365 (4.1x); without them the worst row is ~2.1 s/game, ~1.6x Snow -- INSIDE 3x. A keep table replaces exactly
  those rollouts.
The rows stay on this branch (they carry the GT every check above used); nothing is landed. Whether Vampires enters
the suite over 3x so the mulligan profile can be generated (which brings it into range), or stays out until a perf
route gets it there, is the USER's call (question 45; `MTG_ALLOW_UNTESTED_DECK` is a USER override).

**USER 2026-10-20 (question 45): the gate is hard -- Vampires stays OUT of the suite and needs more work first; the
code lands anyway, provided no other deck breaks.** See "Landing (2026-10-20)".

## Landing (2026-10-20) -- the code on the shared branch, the deck OUT of the suite
- Rebased the 56 `vampires-analysis` commits onto origin `phase-1-2-deck-analyzer` 8e771a391 (78 commits ahead of
  the analysis base) in `.claude/worktrees/vampires-land`, branch `vampires-land`. Hand merges, all with the
  NivMizzetCombo / searched-banked-hold work that landed meanwhile: the Curiosity / Tandem Lookout combat draw
  grant now fires once per damage STEP in the CR 510.4 two-step path (it counted a double striker as two
  events in the single-step loop, which a double striker no longer reaches) -- new unit test; both searched
  combat pins (`scripted_fb_hold`, slot 36, unmoved; `scripted_combat_transform`, slot 37) in the dedup key;
  convoke's reduction applies to X's own cost before Remand's rider joins the bill; GameState 1136 -> 1152
  (both sides' padding fields together, every field already classified).
- **Out of the suite:** the 29 entries (DECK_FILE / DECK_PROF + smoke 4, regression 7, overnight 16) are removed
  and kept as `docs/design/analysis-Vampire.suite-rows.patch` (`git apply` it to re-admit; it applies cleanly
  to `test/regression_cases.sh` as of this landing), with every `vampires*` GT key / `.wins` file and its
  `test/suite_cost.json` entry. The suite cannot see this deck's play again until it is re-admitted.
- **Other decks** (smoke + regression on the rebased binary against origin's ground truth, both GT halves
  restored to origin's first; accepted in 8a83cf620): the moved decks are EXACTLY the predicted set in both
  tiers -- two-step combat (angels, critter) and the payer levers (auras, creature_giving, minotaur, pirates,
  selesnya, soldiers, th); every other deck byte-identical. Searched: 0 slower (19 + 47 faster). d0: 3 slower,
  one class, each restored with both payer levers =0 (soldiers smoke gi49 / gi817, pirates regression gi628:
  the painless payment keeps a life, then the d0 runner sacrifice-cycles a land and is a land short).
- **Reference gate:** the known CritterLifegain/claude_s6_gi5 drift only (question 27). Landing it on the
  shared branch would turn every agent's regression tier red on it, so the shared-branch fast-forward WAITS on
  question 27; the rebased branch is on origin as `backup/2026-10-20/vampires-land`.
- **Question 46:** NOT refilled from this run -- it shared the box with another container's job (load ~25 with
  20 workers; hinata's d5 rows read 3,010 ms/game against 1,468 on 10-19 with byte-identical play). The cache
  keeps the 10-19 refill minus Vampires. A clean `--measure-all` refill is still owed on a quiet box.
- **Next (the first goal):** a route under 3x that does not need a keep table (the keep table is gated behind
  suite membership). 68-76% of the units are pre-keep-table bottoming rollouts, so the folds of questions 40-42
  (a few % each) cannot get there alone; the lever that can is the one NivMizzetCombo's question 15 names --
  the deck's bottom-evaluation setting -- which changes play and is therefore the USER's call.

## Round 2 + integration (2026-10-21) -- under the 3x gate without the mulligan route
- USER 2026-10-21: *"No, I want solutions that speed it up without doing mulligan stuff. Look for proper
  optimizations or branch cuts."* The bottom-evaluation proposal (`vampires-opt` 5bc28feeb) is rejected. Three
  agents from `1a1cf9b9e` (R pass / overrun reuse, P per-node CPU, B branch cuts) plus the decision census and the
  USER's play rules: `docs/design/vampires-search-cost-2.md`, `docs/design/vampires-decision-census.md`.
- The list is 60 cards (USER: *"is this list 61 cards? If so, let's drop Mavren Fein."*).
- Integrated on `vampires-int` (last section of `vampires-search-cost-2.md`). The worst row (2HG d5 s2002) went
  from 243,803 to 83,218 units per game (0.341x) with the USER's rules on, with no slower game from the perf work
  itself beyond budget churn.
- **The gate PASSES:** 2.17x as committed in `test/suite_cost.json` (Vampires from this run, Hinata's cached value),
  0.49x within the same tier run, 1.1-1.2x head-to-head on a contended box. The suite rows are re-admitted on
  `vampires-int` with GT accepted in all three tiers (overnight: the 16 Vampires ENTRY rows only).
- Open for the USER: the two amendments to their rules (`_FREE_DISCARD_V2`, `_ALWAYS_LAND` + `_FODDER`: 11 faster /
  0 slower, net -11, worst row 0.273x, vs net 0 for the rules as written); and the agents' questions (R.8, P.4, B.8).
  The shared-branch landing still waits on question 27.

## Landing (2026-10-21 night) -- rebased onto origin, two interactions found and fixed
- USER: *"we need to work to land it properly, ideally with enough optimizations so it can go in the regression
  test."* `vampires-int` rebased onto origin (84 new commits) in `.claude/worktrees/vampires-land2`. Conflicts: the
  suite maps (union), the lever lists (union), TurnSolver: agent P's `MTG_MOVE_ORDER_KEYS` dropped for origin's
  equivalent `MTG_PLAN_SORT_BY_INDEX`; agent B's playout wave-0 fold merged with origin's ETB-counter fold around
  the same apply (both verify modes: 0 mismatches). GT and the cost table restored to origin's, then re-accepted.
- **Question 27 resolved** without touching the reference: the recording-rule route (`combat_damage_steps` stamp,
  `--legacy-single-damage-step` for older recordings). Reference gate: 630 refs, 0 play-drift, 0 enum-gap.
- **Interaction 1 -- origin's fast plan signature** (`MTG_PLAN_SIG_FAST`) did not carry the Vampires pins (discard,
  madness, steal, convoke cover), so it folded those variants: 9 unit tests failed. Ported;
  `test/check_plan_sig_lockstep.py` now also compares the Action fields each builder reads (it flags the pre-fix
  tree). Fast-signature verify over Vampires + Critter + Soldiers + Angels: 3.57M checks, 0 mismatches.
- **Interaction 2 -- EDF (new on origin) vs the payer's pain-last lever:** Aether Hub's energy colour counted as
  painless, so pain-last spent EDF's single Hub energy to save a life point (smoke d0 gi712: no {W}{W} for Emiel, a T4
  win became a loss; smoke d0 13 slower / 6 faster, regression d0 19 / 5). Fixed by `MTG_PAY_ENERGY_LAST` (an
  energy-paid colour is banded / withheld / guarded with pain; only Aether Hub moves). Held-out EDF A/B, 2,250
  paired games: fixed vs origin's payer 0 / 0 net 0; unfixed +63.
- Every other moved game reproduces its old-base verdict (three-way attribution: origin's base vs origin now vs
  the old-base accept); every Vampires game in all three tiers keeps the old-base accept's kill turn.

## Mulligan profile -- buckets and generation setting (2026-10-21)
- USER ruling (`Vampire.buckets.json`): *"we should at least make it K=18 with Secluded Courtyard and Unclaimed
  Territory merged ... the next best one to merge is probably Voldaren Estate. I will leave that up to what you find
  in bucketing."* Discovery (play setting d5 b20, 400 probes, threshold 0.01) gives **K=15**: {Courtyard, Unclaimed,
  Voldaren Estate}, {Falkenrath Gorger, Falkenrath Pit Fighter}, {Captivating Vampire, Stromkirk Captain} (USER: *"I'm
  okay with that merge"*), every other card alone. Nearest kept-apart pair: Blackcleave Cliffs to the land bucket at
  0.0175; everything else >= 0.0575. K=15 is 116,280 size-7 cells against 346,104 at K=18.
- **Gorger + Pit Fighter (USER: "seems a bit suspect, just because of the Madness option").** Paired A/B, one batch
  (scratch `build/scratch/vampires/mull/gorger/ab.py`): the real deck against a twin whose Gorger carries no
  `grants_madness_creature_subtype` (renamed, so the card numbering and shuffles stay paired). 1,100 games (std d3
  b10 600, std d5 b20 300, 2HG d3 b10 200): **0 kill turns moved**, while every cell's play digest differs. The grant
  IS used -- seed 5100001 game 1 T4: Condemned discards Legion Lieutenant with Gorger out, Lieutenant is madness-cast
  and pumped, 13 damage, the T4 kill -- but it is redundant here: the deck already has 4 Markov Baron and 2 Asylum
  Visitor with their own madness and 2 Bloodghast as free discards. The merge stands (K=15).
- Log gap found on the way: a madness cast is not written as its own `CAST_SPELL` action in the JSON game log; it
  shows only as the outlet's "discarded X" text plus the card on the battlefield afterwards. Follow-up, not fixed.
- Generation setting (the mulligan stage's screen, `mullgen_finalize.py --write`, 48 openers R=24 vs the d5 b20 play
  reference): **d1 b1**, 1,954 units/rollout = 0.26x play, fidelity 1.0000 (rho 0.9965 against a 0.9813 noise
  ceiling). d2 b1 / d3 b1 / d1 b3 are within 4% of it; d3 b20 and d5 b20 cost 3.8-4.0x. Recorded with K in
  `Vampire.profile.json` (`value_play`: no sidecar is created).

- **Generated and ADOPTED 2026-10-21** (`mullgen.sh run decks/Vampires complete`, landed commit eeb55d83a, PGO, 2 h 26 m
  end to end incl. the PGO build and both A/Bs). 161,724 size-7 entries; adaptive keep mean R 7.66 (3-40); every
  bottoming sub cell at R40 (86,402 cells, none at R1). The floor pass printed "COMPLETE ~37 h" -- the stated upper
  bound from the floor rate, which had already paid the full bottoming; the refine took ~25 min.
  Validation: keep A/B -0.159t; confounded bottoming A/B -0.0297t (6/6 seeds, mean/se -11.7). Before adopting, the
  overnight rows were re-sized to the FULL set and accepted with the table ABSENT (496638bd0). Adoption, all three
  tiers: `keep_adoption_attribution.py` 0 unexplained (every changed game a different mulligan count, a different
  bottom, or bottom order only); standard rows -0.03..-0.15t, 2HG mixed on small samples. Cost: the worst searched
  row 2,508 -> 348 ms/game (0.12x Galadriel): the bottoming trial games were the deck's cost.

## Value leaf -- generated and ADOPTED (2026-10-22)
- `valueleaf.sh run decks/Vampires` from `vampires-land2` (frozen 1ed42dcd3, src 208b5152f, PGO), USER-launched
  10-21 21:48 UTC. Needed `deck_registry` to resolve a folder's unique decklist first (1ed42dcd3: Vampire.cod in
  decks/Vampires). Phases 0..E0 ran in 16 min; phase E was cut by a HOST hang at 22:06 (C:\ at 3.1 GB free; the
  path-keyed keep-table cache had grown to 192 GB) and re-ran 10-22 02:48-02:53 after the cleanup, peak 6.3 GB RAM.
- Rows 10,615 from 2,500 games; held-out RMSE 0.321. Matrix 52/52 cells x 400 games, 0 condemned; C0 leaf K=none;
  C.5 CLEAN. Trust candidate d5 REJECTED (never engaged: byte-identical on 8/8 seeds). E.6: no escalation-skip
  allowance (headroom 1.1% < 4%).
- Phase E, 8 seeds x 1,000 paired games vs live (no sidecar): **staged -0.00300 t (t -6.48, 8/0 seeds), 0.945x
  units, 0.78x core-s -- ADOPTED**; emul -0.0005 at 0.977x (passes, not picked); leafless nlesc / nlfit 2.20x /
  2.22x for nothing. The target_depth sweep (d4/d5/d6) read 4.241 on every arm and flagged ZERO VARIANCE --
  informational, not investigated yet.
- GT (vampires keys only, leaf installed): smoke 2 faster / 0 slower (d3 4.2400 -> 4.2333, d5 4.2267 -> 4.2133);
  regression 7 faster / 0 slower (std d3 -0.007 x2, d5 s2002 -0.027, d5 s3003 same score; 2HG d3/d5 -0.02); every
  d0 row byte-identical. Re-measured after rebasing onto origin 9b319e0b2 (the USER's fresh 0.5 x budget_ms
  escalation floor, eb0e7d066): every vampires digest byte-identical to the pre-rebase run, so accepted there.
  Overnight left to the periodic run (no trust moved).
- The sidecar carries no `value_play`, so the profile's generation keys (d1 b1, K=15) still govern play's K guard;
  the next `mullgen.sh run` copies them into the sidecar (mullgen.sh "a sidecar's value_play replaces the profile's").
  The keep table was fitted without the leaf; a regeneration is the USER's call.
- **Play setting decided by the new phase E.7** (USER 2026-10-22: *"The leaf should have a stage that decides."*;
  `valueleaf.sh playset decks/Vampires`, scripts/play_setting_stage.py). The leaf had shipped with no enabled block
  (built-in d5 / 20). Grid on the live leaf, 8 x 1000 paired games per arm (units vs live / delta t):
  d4: b10 1.012x +0.0004, b20 1.182x +0.0001, b40 1.386x -0.0001; d5: b10 0.912x +0.0003, b40 1.087x 0.0000;
  d6: b10 **0.911x +0.0001**, b20 0.999x -0.0001, b40 1.085x -0.0001. Quality flat everywhere; cost not monotone
  (d4 dearest). **ADOPTED d6 / budget 10** (cheapest of 3 passing arms, -8.9% units). A first run forced
  escalation_cap on every arm (the old sweep's convention), which alone cost 1.05x at identical quality; it was
  discarded before any commit and the stage now changes depth x budget only.
  GT (vampires keys): 0 slower / 0 faster. The suite's d5 rows now run d6 at their own budget 20 (block depth + case
  budget) and stayed byte-identical; the d3 rows changed lines at the same scores because an enabled block turns
  the escalation beam on at every depth (AIEngine.cpp vp_beam, "measured neutral+faster").

## Stage 6a disclosure defects D1-D3 (fixed 2026-10-19, branch `vampires-fixn`, cherry-picked 3cd49a776 / 1b1d2d0e9 / a537d4e88)
A read-only code audit compiling the Stage 6a disclosure found three in-window defects; each was reproduced on the
pre-fix engine (every new test failed there), then fixed:
- **D1 (completeness):** madness convoke arms were priced on the PRE-PLAN board and pinned by INDEX, so bodies the
  plan adds first (a Keeper token, a same-plan cast) were invisible and an index could resolve as a different arm or
  a decline. A madness convoke cast is now pinned by its COVER (pips / generic its bodies pay), realised at the
  trigger by the cheapest bodies that pay it there; `MadnessFanFor` offers every cover the trigger's board can pay;
  new subset rule 10 (`SubsetMadnessCoverInvalid`) checks each plan's trigger in executed order (exact partition).
- **D2 (completeness):** the madness path never applied convoke contention, so Q16's free-body dominance dropped the
  land-paid arm even when a later steal needed the untapped Vampires; mirrors the hand-cast exception now.
- **D3 (fold soundness):** rule 9's hand-size exemption was off by one (`h > 7` -> `h + 1 > 7`, the twin's hand), and
  a land-after clone counted the held land (hid an emptied hand under Asylum Visitor).
- Aether Vial's stale bracket note now describes `WantVialCharge`. Melira Pod / Knights byte-identical; Vampires
  60 d3 + 30 d5 paired games: 0 moved. Units 1578/1578.

## Claude-play sweep
Two sweeps (analyze-deck 5d), one Opus player per game, every flag verified against cards.json and the code.
- Sweep 1 (`docs/design/vampires/claude_sweep_1.md`): commit `1c4484e7e`, seeds 91000-91019 (game-index 0), 20
  games, 20/20 tie the search; 12 defects A-L, all fixed (section "Sweep fixes").
- Sweep 2, CONFIRMATION (`docs/design/vampires/claude_sweep_2.md`):
- commit: `2ed53cf74`
- seeds: 92000-92007 (game-index 2-9: opponent creatures, Captivating Vampire's steal exercised in three games)
- games: 8 (8/8 tie the search: T4 x5, T5 x3)
- flags: 0 unresolved (one confirmed defect, M, two independent repros -- fixed 4493e2158; one observation
  dismissed as the documented HumanPlayActive steal design)
- Play changed after the sweep binary only through the M and D1-D3 fixes (each with unit tests that failed before
  the fix and the sweep repros re-run on the fixed binary); every suite row was re-accepted on the final binary.

## Open questions for the user (work continues on the stated default)
1. **Main-2 held-land bar vs Bloodghast** (USER 2026-09-30 "Only newly arrived lands in second main").
   "Pit Fighter sacs Bloodghast post-combat, then play the held land" is unfindable at any budget under the
   bar (main 1 cannot express it: Pit Fighter needs the opponent to have lost life). [default: narrow
   exemption when a graveyard-landfall card is or can be put in our graveyard; env-flagged; PROVISIONAL]
2. **First-strike two-step damage applies to EVERY deck** (a rules-correctness fix: Angels' Lyra lifelink
   feeding Thune / Valkyrie before regular damage; Soldiers' Rick may move). [default: everywhere, GT moves
   explained per game before accept]
3. **Searched discards / sac victims for OTHER decks** (Burning-Fist, Neheb, Troyan, Siege-Gang, Pashalik,
   Psychotrope take the provider's first pick today — an in-window greedy). [default: Vampires only now]
4. **Madness at cleanup**: one-option "cast if payable" (dominance argument in design_A1 §3) vs searched.
   [default: one-option rule, PROVISIONAL] -- LANDED (A1) as DecisionProvider::MadnessCastDefault (base rule;
   the dominance argument is on the hook's declaration); the human is still asked through `madness`.
5. **Convoke tap order** for the attackers it taps: lowest power first vs a provider order keeping Drana /
   Mavren untapped. [default: lowest power first] -- LANDED (A1): CreatureConvokeArms adds COSTLY bodies
   lowest power first; every k is a searched arm, so only WHICH k bodies is the order's choice.
6. **Own-death checks on for Vampires** (Asylum Visitor's mandatory loss, Voldaren Estate's pay-1-life).
   [default: on] -- LANDED (A1): GoldFishRunner arms own_death_live on hellbent_upkeep_life_loss or
   tap_blood_cost (both Vampires-only params; no other deck arms).
7. **Bloodghast "may return" = always yes** in autonomous play (the human is still asked). [PROVISIONAL]
8. **Goldfish-inert collapses** (PROVISIONAL until signed off): Bloodghast can't block; flying (Drana,
   Keeper, tokens); Socialite's menace; Dire Moon's deathtouch; instant-speed activations folded into the
   main phases (+ the in-combat transform window); no priority window between the two damage steps (D-C4,
   ~<=1% of games); simultaneous-entry Socialite counter (D-C3); Captivating never targets our own
   creatures; stolen spawns carry no Auras/Equipment; Lord of Lineage reads mana value 0 on the battlefield.
9. **Stromkirk Captain main-1 vs main-2 on a Drana turn** (all Vampires strike first with Captain out, so
   Drana's counters add no damage that combat) — main split is USER-owned. [default: both mains searched]
10. **Secluded Courtyard paying sac-outlet coloured costs** — mis-model fix; may move other decks that
    run Courtyard + a sac outlet. [default: fix, explain moves] -- LANDED (A2, `MTG_SAC_OUTLET_PAY_SCOPE`
    default ON): smoke play-changed=0; Minotaur's and Soldiers' outlets have no coloured pip, so nothing in
    the suite moves today.
11. **VampiresProvider orders** (discard / victim / activation drafts, design_A2 §8) and, later, the
    Vampires cast order and discard buckets — AGENT-DRAFTED, USER reviews. [landed as ordering hooks,
    `MTG_VAMP_DRAFT_ORDERS` default ON; never narrowing]
12. **VampiresProvider inherits slivers_vial's CLEANUP-discard buckets** from VialProvider
    (`MTG_SLIVERS_BUCKET_DISCARD`) -- the rule the deck rode before it had a provider (Aether Vial routed it to
    Vial). Discard buckets are USER-owned. [default: inherited unchanged (play-neutral vs the old routing);
    the Vampires buckets are yours to set]
13. **Searched discard pins grow `sizeof(Action)` 472 -> 480** for every deck (one InternedName; no padding
    hole holds a pointer). Byte-identical play; a small copy-cost increase everywhere. [default: keep]
14. **Madness emission gate is OPTIMISTIC** (A1): an outlet's cast variants are gated on the turn's optimistic
    mana (OptimisticTurnMana), not on an exact payability probe of the pre-plan board, because the plan may
    play a land / add mana before its trailing pass (an exact pre-plan probe would make "land, then Condemned
    discarding Baron, cast it" unfindable at any budget). Cost: a cast variant that cannot pay at the trigger
    resolves as the decline (a duplicate, never phantom mana). [default: optimistic -- the completeness rule]
15. **Madness / creature-convoke in ROLLOUT LEAVES** (A1): a rollout leaf (estimating, beyond the horizon)
    emits only the provider's front madness variant and convoke arm 0 -- the phyrexian precedent; the searched
    collect and the executor keep every variant. [default: on; PROVISIONAL, the permitted playout policy]
16. **Creature convoke drops the no-convoke cast when a free body helps** (A1; Chord's weak dominance: a free
    body has no other use vs the passive opponent and the land mana it saves is at least as flexible).
    [default: on, PROVISIONAL]
17. **Chord of Calling keeps the LEGACY convoke rules** (A1): the creature-spell terms (every body free after
    combat; a live non-mana {T} body is costly) apply to creature spells only, so Melira Pod is byte-identical.
    Adopting them for Chord is a play change (main-2 arm 0 widens). [default: legacy for Chord]
18. **VampiresProvider declines Bloodghast's madness while the land drop is open** (MadnessCastDefault, draft,
    MTG_VAMP_DRAFT_ORDERS): it orders the fan (decline first) and decides beyond the horizon only. [default: on]
19. **Asylum Visitor's loss to 0 at upkeep** is detected at the post-main-1 own-death check in both worlds (no
    SBA checkpoint between upkeep and main 1 exists; nothing in this list gains life in main 1).
    [default: disclose; PROVISIONAL]

## Track A -- cluster A2 integrated (2026-10-18, integrator; branch `vampires-analysis`, NOT pushed)
Cards (cards.json, verbatim Scryfall costs): Falkenrath Pit Fighter, Stromkirk Condemned, Voldaren Estate,
Blood Token (named token def), Bloodghast. Everything in design_A2 is implemented except where noted below.

**Engine (shared resolvers, both worlds):**
- `DiscardCardFromHand` (SpellEffects.h) -- THE discard hook (A1's name), with `[MADNESS SEAM]` markers; today
  only the new outlets call it (every pre-existing discard site stays open-coded until A1 migrates it).
- Falkenrath Pit Fighter: its own apply `ApplyDiscardSacOutlet` + the one legality predicate `SacOutletLive`
  (restriction + pinned discard + baked victim, checked BEFORE any cost in both trailing passes, by the
  enumerator and by the site-9 key). Cost order = mana -> discard -> sacrifice -> [madness seam] -> draw.
- Stromkirk Condemned: `ApplyActivatePump` mode 4 + `DiscardTeamPumpLive`; `Permanent::once_per_turn_used`
  (reset at our untap in both worlds; BuildSimKey bit 1<<19, PermIsPlainForFoldImpl, FungibilityKey,
  dominance::Build, LandfallTargetFoldKey, RemovalSelfTargetFoldKey, the payment parity check).
- Voldaren Estate: Tier-1 mana (Unclaimed Territory + painland reading); `PermAbilityMode::TapBlood` with
  `TapBloodCostNow` (paid at activation in both worlds); the Blood token `PermAbilityMode::BloodDraw`
  (discard pin via `ActivationDiscardPinScope`, consumed once).
- Bloodghast: graveyard landfall in `FireLandfallTriggers` (snapshot at entry; `revive` chooser in human
  play; autonomous YES -- PROVISIONAL dominance); `Permanent::cond_haste` (derived; `RefreshOppLifeHaste` at
  both begin-combat checkpoints + entry); `GyR_GyLandfallNames` folded into BuildSimKey / dominance;
  `deck_has_landfall` + `deck_has_cond_haste` stamps.
- Secluded Courtyard sac-outlet pay scope (design 0.4): both trailing passes pay every creature-source
  sac outlet under `CreatureAbilityPayScope` (`MTG_SAC_OUTLET_PAY_SCOPE`, default ON, =0 = old payment).
- `DeckUsesSecondMain`: `activate_only_if_opp_lost_life`.
- Sizes: `sizeof(Permanent)` stays 304 (the two new flags + Kili's flag are 1-bit fields in one byte);
  `sizeof(GameState)` stays 1136 (the stamp sits in padding); `sizeof(Action)` 472 -> 480 (one
  `InternedName discard_name`; `int8_t vamp_casts_needed` in padding). No Action size pin exists.

**Search (all branched; providers order only):**
- `Action::discard_name` = the searched discard pin (one variant per distinct non-staged hand name), on Pit
  Fighter / Condemned / Blood crack; folded into plan_signature, ActionFoldSig, BpCandFingerprint,
  PlanDomKey, the action diff printer (all gated on non-empty -> other decks' keys unchanged).
- Pit Fighter victims: one per identity class (LandfallTargetFoldKey) on the pre-plan board + one per
  Vampire creature NAME in hand (a same-plan victim: `victim_name` + `needs_cast_mask` bit1). Source pooled
  (first copy emits; `ActivationFamilyKey` group -> one activation per plan; more through the draw's
  hand-entry breakpoint). Human play: ONE unpinned entry (+ a payability probe), asked at resolution.
- Condemned: per-copy family (`ActivationFamilyKey`), one variant per discard name.
- Voldaren: cost variants k = 0..kmax (Vampires the plan adds), exact-k + a top "k or more" variant.
- `SubsetVampOutletInvalid` (both walkers): over-pinned discards, a same-plan victim without its cast, the
  wrong Blood cost variant, a non-canonical identical-Condemned assignment (odometer positions only).
- Site-9 keys: kActSacOutlet uses `SacOutletLive`; kActTeamPump keys mode 4; TapBlood / BloodDraw appended
  LAST to all four ModeSpec tables (TapBlood priced at `TapBloodCostNow`).
- Land: `TurnSolver::PutsGyLandfallFeeder` adds the -1 ("land after the activations") clone for a plan
  whose activation discards / sacrifices a Bloodghast; the main-2 held-land exemption
  (`GyLandfallHeldLandExempt`, `MTG_VAMP_GY_LANDFALL_HELD_LAND` default ON -- USER QUESTION 1).
- `VampiresProvider : VialProvider` (Stage 4a), routed at the TOP of `DetectDecisionProvider` by
  Vampires-only params from four cards; AGENT-DRAFTED orders (`MTG_VAMP_DRAFT_ORDERS`, default ON):
  ActivationDiscardCandidates, SacOutletVictimCandidates, ActivationOrderRank (Blood -> Condemned -> crack
  -> Pit Fighter). New base hooks `DecisionProvider::ActivationDiscardCandidates` /
  `SacOutletVictimCandidates` (defaults: the cleanup ranking / SacExpendabilityRank, no narrowing).
- Viewer: `discard` / `sacrifice` / `revive` reused (DECISIONS.md rows + a "Vampires A2 outlets" section);
  plan JSON `makes_blood`; menu labels for mode 4 / Pit Fighter / Blood crack; auditor MANIFEST / INERT /
  BOARD_ACTIVATIONS rows (`audit_viewer_decisions.py --no-sweep`: clean).

**Deviations from design_A2 (and why):**
1. Cost order is discard -> sacrifice (A1 §2.5 / the integration brief), not sacrifice -> discard: the madness
   card is in exile while the victim is chosen, so it still can never be the victim.
2. Same-plan Pit Fighter victims are emitted at ENUMERATION (named, guarded) instead of the post-dedup
   `AppendSubdecisionAxes` re-point: no hand-index reconstruction, and the subset guard makes a missing cast
   a rejected duplicate. Same reachable set.
3. Condemned: per-copy emission + a canonical-assignment subset rule instead of a k-count multiset action
   (same canonical set, no new Action fields).
4. Blood cost variants keep EXACTLY one variant per subset (exact k, top variant "k or more") -- a ">= k"
   guard alone would enumerate each subset once per k it satisfies.
5. The "unpinned autonomous apply in the window" invariant is a counter (`g_activation_discard_unpinned`,
   asserted 0 by the lockstep unit test), not an abort -- there is no debug build to abort in.
6. Blood-crack pooling = the first untapped Blood (identical untapped Bloods have no distinguishing state).

**PROVISIONAL (user sign-off owed):** Bloodghast "can't block" inert; instant-speed timing of Pit Fighter /
Condemned / Blood (and Blood creation) collapsed to the main phases' trailing passes; Bloodghast's "may"
= YES autonomously; Voldaren "Vampire spell" == "creature spell" (exact for this list); the
main-2 held-land exemption (open question 1); the VampiresProvider draft orders (open question 11).

**Verification (A2, 2026-10-18):** `mtg-test` 1464/1464 cases pass (new `test_vampires_outlets.cpp`: card
data, Voldaren mana contexts incl. Courtyard under the creature-ability scope, Blood cost / creation /
crack, Pit Fighter liveness / apply / self-sac / enumeration folds / same-plan victims, Condemned
once-per-turn + fold keys + identical-copy symmetry, Bloodghast return / revive decline / conditional
haste, the -1 land after a Bloodghast discard, the main-2 held-land exemption, a rollout-vs-executor
lockstep sweep over every main-2 activation plan with `g_activation_discard_unpinned == 0`; the routing
table pins Vampires -> Vampires and survives cutting any one A2 card). Smoke: 128/128 PASS, searched and
d0 `play-changed=0` -- byte-identical for every suite deck, including the two other decks that pair a
Secluded Courtyard with a sac outlet (Minotaur: Slaughter-Priest's {2} is generic, which Courtyard's {C}
already paid; Soldiers: Ranger-Captain / King Darien / Jirina outlets cost no mana) -- so the pay-scope
fix moves nothing in the suite today. `provider_audit.py --check` rc 0 (Vampires has no profile yet:
"NOT AUDITED"; the unit test pins its routing). `audit_card_costs.py` rc 0 (441/441 compared).
`audit_card_fields.py`: every hard field (cost, P/T, types, keywords) of the new cards matches the
snapshot (Voldaren Estate / Bloodghast entries built from the stored Scryfall JSON after a 429; the
audit's rc 1 is the pre-existing unfetched Temple of Abandon). `audit_viewer_decisions.py
decks/Vampires/Vampire.cod --no-sweep`: clean. Sanity
`mtg decks/Vampires/Vampire.cod --games 20 --seed 777 --depth 3 --budget-ms 20 --threads 8`: no crash, avg
6.55 (4 unwon -- 9 of the 20 cards still unimplemented); one SLOW-GAME (seed 790 gi13, 66 s in the batch,
2:42 single-threaded, 4.9M units) -- a flat profile (BuildSimKey 5.9%, operator new 4.3%, CollectActions
2.6%), i.e. broad search over dead hands, not one hot new path. Watch it at the 3x suite-cost gate.
UN-RUN: a hand-built `--claude-play --choices` walk to a Pit Fighter / Condemned / Blood activation (not
cheap to steer; the decision surfaces are the existing `discard` / `sacrifice` / `revive` paths, covered
by the unit tests' revive-decline case) -- owed to Stage 5h.

**Seams for the madness integrator (cluster A1, next):**
- `DiscardCardFromHand(state, controller, hand_idx, cause)` (SpellEffects.h) is the hook; add the
  `std::vector<MadnessPending>* mq` parameter and the `MadnessCostsFor` exile there. Its three callers are
  exactly the `[MADNESS SEAM]` markers: `ApplyDiscardSacOutlet` (after `SacrificePermanentAt`, before
  `TrickDraw` -- the discard is BEFORE the sacrifice, so a Gorger sacrificed to Pit Fighter must not revoke
  the snapshotted grant), `ApplyActivatePump` mode 4 (after the discard, before the pump), and
  `ApplyPermAbility` BloodDraw (after the token sacrifice, before the draw). ApplyPermAbility has no payer
  parameter: thread the `MadnessPayer` the way `ActivationDiscardPinScope` threads the pin (both trailing
  passes install it around the call).
- The madness pin rides the SAME action as `Action::discard_name` (TurnSolver.h, beside it); fold it at
  every site `discard_name` is folded (grep `discard_name`: plan_signature's SAC / APUMP / PABIL keys,
  ActionFoldSig, BpCandFingerprint, PlanDomKey, the action diff printer).
- The discard fan is built in ONE place per outlet: `ActivationDiscardClasses` (TurnSolver.cpp) feeds the
  Pit Fighter block ("FALKENRATH PIT FIGHTER" in the value-outlet loop), the Condemned mode-4 block and the
  Blood-crack block; cross each madness-capable discard class with decline + payable options there.
  Human play emits ONE unpinned action per outlet (the card -- and A1's madness answer -- is asked at
  resolution).
- `TurnSolver::PutsGyLandfallFeeder` must NOT count a discard whose madness pin CASTS the card (it does not
  reach the graveyard). `SubsetVampOutletInvalid` rule 1 counts pinned discards per name -- unchanged by
  madness. A madness-cast Vampire lowers Voldaren's Blood cost only if Blood creation runs after it
  (provider ActivationOrderRank puts Blood creation FIRST in the trailing pass today).
- My outlet sites already increment `cards_cycled_or_discarded_this_turn` themselves (the hook does not).

## Track A -- cluster A1 integrated (2026-10-18, integrator; branch `vampires-analysis`, NOT pushed)
Commits `34ccb465a` (engine + viewer + unit tests) and `51a60b624` (Scryfall snapshot, audit tweak). Cards
(cards.json, verbatim Scryfall costs): **Falkenrath Gorger** {R}, **Markov Baron** {2}{B}, **Asylum Visitor**
{1}{B} -- every clause modelled (design_A1 §1); `Keyword::Madness` (inert tag) maps Scryfall's keyword.

**Engine (shared, both worlds):**
- THE DISCARD HOOK `DiscardCardFromHand(state, ctl, idx, mq*, cause)` now carries EVERY real discard: the cleanup
  (`GameEngine::CleanupStep`, `CleanupDiscardShed` + the legacy loop), Burning-Fist (`ApplyActivatePump` mode 1),
  Neheb (`FireNehebCombatDamageDiscards`), the Troyan loot, `ApplyChannel`, `ApplyTransmute`, Celes's rummage,
  Gamble (`PerformTutor`), retrace (both worlds), cycling (D1/D2/D3), Land's Edge (both worlds, hand order kept),
  the dormant `ChooseDiscard` probe trial, and A2's three outlets. A madness card (own `madness_cost`, or a
  `grants_madness_creature_subtype` granter we control -- snapshotted at the discard) goes to exile and is queued;
  `GameState::deck_has_madness` (padding, sizeof unchanged at 1136) skips the lookup for every other deck.
- `ResolvePendingMadness` (FIFO; pin -> option by (cost, arm), stranded -> decline; unpinned -> provider
  `MadnessCastDefault`; human `madness` chooser) pays through a CALLER-OWNED creature-SPELL payer
  (`MadnessHostScope`: rollout `RolloutMadnessPayer` = TapForCostDirect, executor `AIEngine::MadnessPayer` =
  TapForCost + fresh pool; never inside the outlet's ability pay scope). `CastFromMadness` = the cast bookkeeping
  (storm / MV counters, Irencrag budget, on-cast triggers, prowess) + the shared enter cascade (D-A1-1: no
  stack traversal, provably inert for the pool). Order at the outlets: discard -> sacrifice -> madness -> effect
  (Pit Fighter's lone Gorger still grants; Condemned's pump reaches the madness-cast creature). Cleanup: after
  the CR 514.2 reset, the step's float emptied, the provider rule.
- Convoke generalised (`ConvokeRules`, `ClassifyConvokeBodiesEx`, `ApplyConvokeReduction(.., pip)`,
  `ApplyConvokeTapsForCard`, `CreatureConvokeArms`): a creature spell's pip colour, every body free after
  combat / at cleanup, a live non-mana {T} body costly (`ConvokeBodyTapAbilityLive` -- cluster B adds Bloodline
  Keeper's param there). Chord keeps the legacy rules (open question 17). Every recompute site passes the pip.
- Asylum Visitor: `PerformUpkeepHellbentDraw` (top of `GameEngine::UpkeepStep` / before the rollout's suspend
  arrivals; intervening-if on non-staged cards; one draw for N Visitors; empty library = the draw-step loss) and
  `PerformOpponentUpkeepHellbentDraw` (before `opponentdeck::EndOfTurnDraw` in both worlds; only with a dealt,
  empty opponent hand; each Visitor resolves). Own death armed (question 6).
- `greedywindow` moved verbatim to `src/core/GreedyWindow.h` (the core resolver's in-window counter reads it).

**Search:** `Action::madness_choice / madness_arm / madness_mv` (in padding: sizeof(Action) stays 480).
`MadnessFanFor` crosses each madness-capable outlet discard pin with decline + one cast per (cost x convoke arm),
provider default first, gated on the optimistic turn mana (question 14); a Gorger IN HAND opens the grant and
`SubsetVampOutletInvalid` rule 5 requires its cast. Folded into plan_signature (#M), ActionFoldSig,
BpCandFingerprint, PlanDomKey, the diff printer (all gated on >= 0). `madness_mv` rides `a.cost` as generic;
the six trailing-pass pay sites pay `OutletOwnCost(a)`. `PutsGyLandfallFeeder` ignores a discard whose pin casts
the card. Baron's hard cast: a CollectActions post-pass (arm 0 replaces the base when it taps a body; arms k
searched; rollout leaves keep arm 0); the emit prune exempts convoke creatures and gates each arm on its reduced
cost. Counters (`MTG_ROLLOUT_STATS`): `madness_variants`, `madness_unpinned_in_window`, `madness_unhosted`.

**Viewer:** bucket-B `madness` (GameLogger `MadnessChooser` + RevealLogPause / ComboOffApplyPause nulling;
call site ResolvePendingMadness, asked only for UNPINNED triggers with a payable cast; `WriteMadnessDecisionJson`
+ RunClaudePlay lambda; `madnessPanelHtml` centred card-image modal; SUBDECISIONS; DECISIONS.md row; audit
MANIFEST `madness_cost` / `grants_madness_creature_subtype` -> madness, INERT the three Visitor params --
PENDING USER OK per the self-guard). Baron's convoke arms ride `main_phase` plan variants (the Chord precedent).

**Deviations from design_A1 (and why):**
1. The pin names (cost index, convoke ARM), not raw tap counts -- a plan whose earlier casts add free bodies
   still realises "every free body"; a pinned option that no longer exists resolves as decline.
2. Emission gate optimistic, not an exact copy probe (question 14). The apply-time options ARE probed exactly.
3. A Gorger in hand opens the fan + rule 5 (the design's emission only read the pre-plan board, which would make
   "cast Gorger, then discard a Vampire for madness" unfindable).
4. Rollout leaves keep the front variant / arm 0 (question 15).
5. The human is asked only when a cast is payable (a forced decline is not a decision; the event log says it).
6. Creature-spell convoke rules are keyed on the card being a creature; Chord stays legacy (question 17).
7. Resolution-time discards outside a hosted pass (Neheb in combat, Gamble, Celes outside the trailing pass)
   have no payer -> decline (unreachable today: no current deck pairs them with a madness card).
8. `MTG_DISCARD_SHED_VERIFY` was not extended to madness outcomes (it compares shed NAMES; the madness outcome is
   covered by the unit sweep instead).

**Verification (A1, 2026-10-18):** `mtg-test` **1499/1499** (new `test/unit/test_vampires_madness.cpp`, 18
cases: card data; MadnessCostsFor incl. scope / fold / gate; the hook; decline / cast / unpayable / unpinned;
Territory pays the madness SPELL but not an ability; Pit Fighter sacrificing the lone Gorger still casts;
Condemned pumps the madness-cast Baron; convoke arms pre/post combat + Chord byte-identity; Baron's arm
enumeration; the madness fan (decline AND cast survive the dedup, both realise); a Gorger in hand + rule 5; two
Gorgers = one option; cleanup in both worlds (cast / decline, float emptied); Visitor ours (two Visitors = one
draw, staged-only hand, empty library) and the opponent's; the chooser (asked unpinned, nulled under
RevealLogPause, not asked when pinned); a rollout-vs-executor lockstep sweep over madness / convoke plans in
both mains with `g_madness_unpinned_in_window` unchanged; routing). **Smoke** (`regression.sh --smoke`, NOT
accepted): `120 passed, 8 failed`, scenarios 217/217 -- exactly cluster C's 8 rows (critter d0/d3/d5/2hg,
angels d0/d3/d5/2hg) with C's values and C's per-tier counts (searched faster=19 play-changed=136, d0 faster=65
play-changed=459, 0 slower); **every other deck byte-identical**, including every deck whose discard site moved
(Melira Pod's Chord, Minotaur's Burning-Fist / Neheb, Hinata's Gamble, Treasure Hunt's Land's Edge / retrace /
cycling, Bruna's Troyan, FiveColour's Progenitus / transmute). **Madness decision repro** (the real resolution
paths): cleanup -- `build/Release/mtg decks/Vampires/Vampire.cod --claude-play --seed 101 --game-index 0
--max-turns 9 --choices 1,2,-1,-1,4,-1,-1,11,-1,-1,11,11,0` -> exit 70, `madness` (source `cleanup`, Markov
Baron, option 0 "Cast for {2}{B}"); reply 0 puts Baron onto the battlefield, -1 into the graveyard. Outlet --
`--choices 1,2,-1,-1,4,-1,-1,11,-1,-1,9,-1,-1,77,0` (T5: Stromkirk Condemned's unpinned entry -> `discard` ->
Baron) -> exit 70, `madness` (source `Stromkirk Condemned`, options "Cast for {2}{B}" and "Cast for {2},
tapping 1 creature (convoke)"). **Sanity** `mtg decks/Vampires/Vampire.cod --games 40 --seed 777 --depth 3
--budget-ms 20 --threads 16`: no crash, avg **4.5000** (22 T4 / 16 T5 / 2 T6, **0 unwon**), 4.05 s wall at 16
threads, no game over 10 s (`--batch` SLOW-GAME), `madness_variants=219751 madness_unpinned_in_window=0
madness_unhosted=0`; a crash check at d0 (200 games, seed 4242: avg 4.915) and d5 b20 (20 games, seed 9100: avg 4.600) also kept both counters at 0. `audit_card_costs.py` **rc 0** (448/448 compared). `audit_card_fields.py --update`
(targeted) + offline: every hard field matches (rc 1 = the pre-existing unfetched Temple of Abandon).
`provider_audit.py --check` rc 0 (Vampires NOT AUDITED: no profile yet; the routing test pins Vampires ->
Vampires and Melira Pod -> MeliraPod). `audit_viewer_decisions.py --no-sweep`: expected {discard, madness,
revive, vial_charge}, oracle cross-check clean. `test/viewer_decision_types_check.js`: 37/37 answerable.

**Notes for the next integrator (cluster B: Captivating Vampire, Bloodline Keeper // Lord of Lineage):**
- Bloodline Keeper's "{T}: Create a 2/2 black Vampire token with flying" makes it a COSTLY convoke body: add its
  param to `ConvokeBodyTapAbilityLive` (SpellEffects.h) -- the creature-spell convoke rules then never count it
  free (and the post-combat collapse still leaves it costly). Captivating Vampire's "Tap five untapped Vampires
  you control" competes with convoke for the same bodies -- that is a cost on OTHER creatures, not a {T} on
  itself, so it does not belong in that predicate; the search already sees both (casts / activations in one plan).
- Madness casts enter through the shared enter path (`CastFromMadness` -> FireEtbWatchers / FireOwnEtbTriggers),
  so Keeper's five-Vampire transform count and Captivating's lord bonus see them for free.
- `sizeof(Action)` is 480 with A1's three int8 pins in padding; `sizeof(GameState)` 1136. No Action size pin
  exists. The outlet trailing passes now install `MadnessHostScope` around every ApplyPermAbility /
  ApplyActivatePump / discard-sac apply -- a new activation kind that can discard must do the same (both worlds)
  and fan its discard pin through `PushWithMadnessFan(actions, a, MadnessFanFor(...))`.
- Bloodline Keeper is still a name-only placeholder in the deck (cost "" in the viewer); it is never castable
  until B lands.


## Track B -- cluster B integrated (2026-10-18, integrator; branch `vampires-analysis`, NOT pushed)
Commits `8a0faa4cf` (engine + cards + viewer + unit tests) and `d63ce1889` (Scryfall snapshot, audit rows).
Cards (cards.json, verbatim Scryfall costs): **Captivating Vampire** {1}{B}{B}, **Bloodline Keeper** {2}{B}{B}
(was a name-only placeholder -- never in cards.json), **Lord of Lineage** (back face, `mana_cost ""`, colour
indicator B); the design §7 note lines appended to **Unclaimed Territory** / **Secluded Courtyard**; both
Keeper faces' card-level `Transform` keyword allowlisted in `scryfall_divergences.json`.

**Engine (shared resolvers, both worlds):**
- `PermAbilityMode::TapSubtypeSteal` (appended last; in `PermAbilityTaps`' exclusion list). Params
  `steal_tap_count` / `steal_tap_subtype` / `steal_adds_subtype`. Liveness (`PermAbilitySourceLive`, under a
  `StealPinScope`): the pinned target is still a creature we do not control and the pinned tap classes still
  have their untapped members -- no substitution. The apply (`ApplyPermAbility` case) chooses the TARGET
  first (CR 601.2c), then pays the cost through ONE payer `PayTapSubtypeCost` (never `CanTapNow`: sick bodies
  and the source itself are legal -- the 2010-08-15 ruling), then: `controller_index`, `gained_control_this_
  turn`, `+Vampire` (if absent), tapped status untouched, owner kept. `SacrificePermanentAt` now puts a card in
  its OWNER's graveyard (a stolen spawn sacrificed to Pit Fighter goes to the opponent's; byte-identical for
  every owned permanent).
- `PermAbilityMode::TransformSelf` (appended last; no {T}). Params `transform_back_name` / `transform_front_name`
  / `transform_cost` / `transform_min_controlled_subtype` / `_count` (NOT the daybound params). Liveness counts
  Vampire PERMANENTS (`CountControlledSubtypePermanents`) at activation; the {B} is paid under
  `CreatureAbilityPayScope` (Courtyard yes, Territory / Estate restricted mana no). The swap reuses
  `SwapDayboundFace` (same Permanent: number, counters, tapped, `entered_this_turn`; no enter cascade).
  Leaving: `NormalizeLeftDayboundFace` keys on `transform_front_name` too (Lord of Lineage -> Bloodline Keeper
  card). `CardDatabase::MdfcBackFaceNames` lists Lord of Lineage.
- In-combat window: `Plan::combat_transform_choice` (int8, sizeof(Plan) stays 464) -> `GameState::
  scripted_combat_transform` (int8 in padding, sizeof(GameState) stays 1136) -> `ApplyCombatTransforms`
  (SpellEffects.h) right after `ApplyCombatAuraSwap` in `GameEngine::CombatPhase` and `SimulateCombat`, plus
  the projection twin `CountCombatTransformPump`. Folded in the plan hash, SamePlan, IsApplyEmptyPlan,
  PlanIsAxisVariant, PlanHasCombatPin, the census, the dedup key's combat-pin slots, dominance::Build, the
  executor's realised-pin check; cleared at both turn starts.
- Keeper's live token tap makes it a COSTLY convoke body (`ConvokeBodyTapAbilityLive`, via `TapTokenSourceLive`;
  only creature-spell convoke reads it -- Markov Baron).
- Deck stamps `deck_has_transform` / `deck_has_tap_steal` (padding).

**Search (everything branched; nothing collapsed to one option in the window):**
- STEAL variants (`EmitStealVariants`): target = one per identity class of creature we do not control
  (`LandfallTargetFoldKey`), representative = lowest number; tap set = one per distinct count vector over the
  body classes summing to 5 -- FREE (cannot attack this turn: colour mask + live {T} ability) and ATTACK
  (full identity, `StealAttackBodyKey`) -- counting the bodies the plan can still ADD before its trailing pass
  (hand Vampires cast / Vial-put / madness-cast, Keeper tokens, a graveyard Bloodghast while the drop is open).
  The spec is interned (`StealTapSpec`, `Action::steal_spec` in the hole after `kind` -- sizeof(Action) stays
  480); every key folds its CONTENT (plan_signature `#T<target>#S<spec>`, ActionFoldSig, BpCandFingerprint,
  PlanDomKey, the diff printer). Both mains. A Captivating cast THIS plan is a source (needs_cast_mask bit0,
  re-resolved by name at apply: `ResolveHandSourceActivation`).
- TRANSFORM (`EmitTransformAction`): one per untransformed source (board or same-plan cast), gated on the
  UPPER BOUND of Vampires the plan can add (`SubtypeCountUpperBound`), exact restriction at apply.
- Combat window axis (AppendSubdecisionAxes): one variant per non-empty subset of the transform sources at
  the window (<= 3), pre-combat outer lists only, only while an attack-token source of the subtype is on our
  side or cast by the plan, and only when the plan's pre-combat Vampire count CANNOT already reach 5 (the
  identity fold: then the main-1 transform of the same plan reaches the same board).
- Subset rule 6 (`SubsetVampOutletInvalid`): a hand-source activation needs its source's cast; a variant whose
  arrivals (`vamp_casts_needed`) exceed what the selection can possibly add is a no-op duplicate (a necessary
  condition, counted generously).
- Trailing COUNT BANDS (`OrderTrailingActivations`, both worlds; only when a steal / transform is in the
  pass): everything else in provider order (Keeper token taps, Blood creation, Condemned, Blood crack) ->
  the steal -> the transform -> creature sacrifices (Pit Fighter). Sound: "token, then transform" and
  "Condemned's madness Baron, then steal with it" are both findable.
- Attack projection: a steal's ATTACK-class bodies are discounted (`AttackTapCtx::cand_mask`).
- Rollout leaves emit only the front steal variant (the madness / convoke-arm precedent -- the permitted
  playout policy). Unpinned autonomous steals are counted (`g_steal_unpinned`, must read 0 in the window).
- Routing: VampiresProvider's signature ORs `steal_tap_subtype == "Vampire"` and
  `transform_min_controlled_subtype == "Vampire"`.

**Viewer:** verbs `steal=` / `transform=` (plan JSON `verb`, LineSpec, CheckLine legality + matching + the
pass-check list, GUI labels at the three sites); the steal's target and five bodies are board-click `target`
prompts at resolution (forced picks not prompted); the combat transform is `dig` "(combat transform)"
(emitter note + `digPanelHtml` branch). DECISIONS.md: the verbs table, a `target` reuse row, a `dig` reuse
row, the `taptoken=` row names both Keeper faces. Auditor: MANIFEST `steal_tap_count` -> target,
`transform_back_name` -> dig, BOARD_ACTIVATIONS `verb:steal` / `verb:transform`, MAINPHASE `transform_cost`,
INERT the detail params, two STATE-GATED rows, the oracle cross-check models the steal's target.

**Deviations from design_B (and why):**
1. Tap set = an interned CLASS multiset (FREE colour/{T} codes + ATTACK representatives), not five pinned
   card numbers: bodies the plan itself adds have no number at enumeration, and a convoke cast that taps one
   twin is absorbed by the other. The FREE key drops "is source": tapping the source vs a same-colour body is
   unobservable (nothing reads Captivating being tapped).
2. `PayTapSubtypeCost` is called INSIDE the `ApplyPermAbility` case (which both trailing passes reach), not by
   the two apply sites before it -- still one shared payer, and target-before-cost holds there.
3. Trailing bands: the steal runs AFTER the provider-ordered band-0 activations (not beside the token taps):
   Condemned's / the crack's madness casts can then pay it; Blood creation loses nothing (with five Vampires
   out it already costs {0}).
4. Combat window: also seeds an empty "pass" variant when the drop is spent (the enumeration offers no empty
   plan then, and Solve's own pass never reaches the fan) -- otherwise "attack with the Keeper, transform on
   Mavren's token" was unfindable on drop-spent turns.
5. No site-9 activation keys / BP_NEW_ONLY availability bits for the two modes: a continuation's steal or
   transform therefore always reads as NEW (the safe direction; never drops a line).
6. Lord of Lineage's battlefield MV stays 0 (PROVISIONAL): not cheap -- Card has no MV field separate from
   its mana cost, and copying the front's cost would give the face a mana cost (devotion).
7. The steal variants' ORDER (move ordering + the rollout leaf's front) is a neutral engine eval (target power
   first, then least attack power given up, then fewest arrivals), not a provider hook (open question 22).

**Verification (B, 2026-10-18):** `mtg-test` **1514/1514** (new `test/unit/test_vampires_lords.cpp`, 16 cases:
card data + back-face art list; Keeper token (sick -> none; 2/2 black flying sick Vampire, Lieutenant +1;
live tap = costly convoke body); transform restriction 4 vs 5 (token counts, opponent's Vampire does not;
sick / tapped Keeper legal); swap fidelity (number, counter, tapped, sick; no Socialite counter; others +2/+2,
not itself; one-shot); Lord sacrificed -> Bloodline Keeper card; payment (Swamp / Courtyard yes, Territory
no); reachability (fifth Vampire cast this plan with rule 6b; token-then-transform in both action orders; a
Keeper cast this plan); the combat window (axis fans it; unpinned holds; pinned transforms after Mavren's
token, pays {B}, +2 to the attacker; no fifth Vampire = clean no-op); steal legality (4 no, 5 with sick
source yes, tapped source no, no target no); steal effect (five tapped incl. sick + itself, controller /
owner / sickness / Vampire / three lords, next-turn attack, owner's graveyard); folds (three 1/1s = one
target class; tap classes fold the source with same-colour bodies -- 4 sets of 6 choose 5); stale pin = no-op;
same-plan bodies + self-tap of a Captivating cast this plan; rollout-vs-executor lockstep over every steal /
transform / token plan in both mains with `g_steal_unpinned` unchanged; routing on the B cards alone).
**Smoke** (`regression.sh --smoke`, NOT accepted): `120 passed, 8 failed` -- EVERY case digest identical to the
A1 run, i.e. exactly cluster C's 8 rows (critter d0/d3/d5/2hg, angels d0/d3/d5/2hg), searched faster=19
play-changed=136, d0 faster=65 play-changed=459, 0 slower; nothing else moved. **Sanity**
`--games 40 --seed 777 --depth 3 --budget-ms 20 --threads 16`: avg **4.4250** (24 T4 / 15 T5 / 1 T6, 0 unwon;
was 4.5000), 4.87 s wall (was 4.05 s), 931 core-ms/game; `--games 20 --depth 5 --budget-ms 40`: avg **4.4000**,
5.46 s wall, 1,291 core-ms/game; no game over 10 s (`--batch`, MTG_SLOW_GAME_MS=10000). Well under the 8,907
ms/game suite cap. `audit_card_costs.py` **rc 0** (450/450 compared). `audit_card_fields.py --update`
(targeted) + offline: every hard field matches (rc 1 = the pre-existing unfetched Temple of Abandon).
`audit_viewer_decisions.py --no-sweep` **rc 0**, oracle cross-check clean; `viewer_decision_types_check.js`
37/37; `provider_audit.py --check` rc 0 (Vampires NOT AUDITED: no profile yet).
**Viewer repros** (claude-play, real resolution paths):
- steal TARGET: `build/Release/mtg decks/Vampires/Vampire.cod --claude-play --seed 128 --game-index 8
  --max-turns 9 --choices 1,0,0,0,0,0,0,0,0,0,0,0,0,0,0` -> exit 70 `target` (source Captivating Vampire,
  "taken under your control (it becomes a Vampire)", 6/6 and 1/1); the T5 menu's plan 0 carries
  `"verb": "transform"` + `"verb": "steal"`; reply 0 -> the 6/6 is ours (the five bodies were forced: exactly
  five untapped Vampires).
- steal BODIES: `--choices 1` + eighteen `0` -> `target` "tapped to pay its cost (1 of 5)" (T6; the lone 1/1
  is a forced target), Captivating itself and the stolen 6/6 among the legal bodies.
- combat transform: `--seed 254 --game-index 8 --max-turns 9 --choices 1,0,0,0,0,0,0,1,0,0,0,1,0,0,0,1,0,1`
  -> exit 70 `dig` "Bloodline Keeper (combat transform)", default -1; reply 0 -> "Bloodline Keeper transforms
  into Lord of Lineage", the attack deals 19 (13 with -1).
- `--validate-line "transform=Bloodline Keeper;steal=Captivating Vampire"` (same T5 state) -> accept plan 0;
  `steal=` alone -> plan 2, `transform=` alone -> plan 1.

**PROVISIONAL (user sign-off owed):** own creatures not steal targets; no opponent-turn window (steal, token
tap); attached Auras/Equipment on a stolen creature unmodelled (spawns carry none); Lord of Lineage MV 0;
Flying (Keeper, Lord, tokens) inert; one steal per Captivating per main phase.

## Open questions -- cluster B (work continues on the stated default)
20. **Steal targets exclude our own creatures** (a legal no-op; the Bolas -2 precedent). [default: excluded,
    PROVISIONAL]
21. **Rollout leaves take only the front steal variant** (the playout policy beyond the horizon; the searched
    collect and the executor keep every variant). [default: on, PROVISIONAL]
22. **Steal variant ORDER lives in the enumerator** (target power, then least attack power given up, then
    fewest arrivals) -- ordering only, and the rollout leaf's front. Should it be a VampiresProvider hook
    (USER-owned like the other Vampires draft orders)? [default: engine-neutral order, keep]
23. **Combat-transform fold**: no combat variant for a plan whose pre-combat count can already reach five (its
    main-1 transform reaches the same board from the same mana). [default: on -- an identity fold]
24. **The combat transform's {B} has no main-phase mana hold**: if the plan's casts tap the only black source a
    combat variant resolves as its base plan (a duplicate). Not observed in the repros; a hold (the aura-swap
    shape) is a follow-up if the sweep shows it. [default: no hold]
25. **Lord of Lineage's battlefield mana value reads 0** (CR 712.8e says 4) -- unobservable in this list.
    [default: disclose, PROVISIONAL]
26. **A sacrificed permanent goes to its OWNER's graveyard** (SacrificePermanentAt) -- a rules fix for every
    deck, byte-identical in the suite (no suite deck sacrifices a creature it does not own). [default: keep]

## Open questions -- orchestrator / cluster C (work continues on the stated default)
27. **CritterLifegain/claude_s6_gi5 now replays T4 (recorded T5)** under the faithful first-strike step (see
    Status). The checker has no override mechanism by design, so the regression tier's reference gate stays red
    on this one reference until the USER rules. [default: reference untouched; drift disclosed]
    **RESOLVED 2026-10-21 (USER: "we need to work to land it properly")** through the checker's existing
    recording-rule route, the one `karoo_float` / `pain_defer` / `gsz_copy_id` use: the reference writer now
    stamps `combat_damage_steps`, and a reference without it replays under `--legacy-single-damage-step`
    (the one summed step it was played with). The reference is untouched and replays T5 as recorded; a game
    recorded from now on replays under the two-step rule. `tools/play/DECISIONS.md`, unit test in
    `test_vampires_combat.cpp`.
28. **Baseline profile `min_playable: 0`** (the analyzer's output; Soldiers / Pirates / Colossus ship the same;
    the skill's old note asks >= 1 for multi-colour decks). Keep policy is USER-owned and the keep table
    replaces it. [default: as generated]
29. **Attack projection does not count first-step lifelink feeding Thune / Pridemate / life thresholds**
    (integration_C_notes): the search's damage estimate undercounts there (the safe direction); rollouts are
    exact. [default: leave]
30. **Rick, Steadfast Leader keeps choosing vigilance + lifelink over first strike** -- correct for Soldiers,
    which has nothing between the damage steps. [default: unchanged]
31. **New combat params classified as no-viewer-decision** (INERT_PARAMS). [default: as classified]

## Sweep fixes (Stage 5d claude-play convergence, 2026-10-18; branch `vampires-fix`, NOT pushed)
Fix integrator over the sweep's defect list (items A-L; repro CSVs in the sweep scratch DEFECTS.md). Commits
`c2d643522` (A-E, G-K), `6939bd4d3` (F + L, the payer sub-integrator's `6e6497968` cherry-picked) and the F x L
follow-up (the activation-line hold). The old CSVs do not replay index-for-index on the new binary (the convoke
arms and the human filters change the menus), so every repro below was re-driven with a summary-matching
translator (scratch `fix/repro/translate.py`) and the human menus checked by an all-or-nothing probe
(`fix/repro/allornothing.py`: execute every plan holding a Vampires activation, verify each listed activation's
event).

**A -- phantom convoke mana (SERIOUS).** Root cause: the creature-spell convoke bodies were tapped in a PRE-PASS
over the whole plan (executor TakeTurn / ApplyPlanDirect / the continuation replays), each Baron's committed
(pip, other) counts against the pre-plan board, while every payment site reduced the cost by the COMMITTED
counts -- two Markov Barons on the same three bodies tapped three creatures and were both reduced by three (seed
91011 T3: "Territory; Baron, Baron" resolved with all three lands untapped). Fix: creature-spell convoke taps AT
THE CAST (`ConvokeCastTaps`, SpellEffects.h), cheapest first on the board as it stands, and the cost comes down by
the bodies ACTUALLY tapped -- one helper in CastSpellFromHand, apply_one and the sequential-payability walk; a
failed payment untaps them; the pre-pass is a no-op for creature spells (Chord keeps it: Melira Pod
byte-identical). Subset rule 7 (`SubsetConvokeBodiesInvalid`, both walkers) refuses a selection whose arms claim
more bodies than the board plus the plan's own arrivals hold (the last convoke cast serves nobody). Under
contention (a second convoke creature, a steal source) fewer-free arms down to the plain cast are offered, so
"Baron A takes the bodies, Baron B pays lands" stays findable. Tests: `test_vampires_convoke.cpp` "Convoke A" x2
(mana conservation in both worlds over every two-Baron plan; a stale arm reduces only by its real taps and
untaps on failure). Repro: 91011 T3 menu holds no phantom plan (the 9-mana "Baron, Baron, Captain" and the
land-less "Baron, Baron" are gone) and every two-Baron plan executes conserving mana (e.g. plan 8: Bloodghast +
Dire Moon + the first Baron + 3 lands = 6).

**B -- same-plan convoke bodies (reachability).** Root cause: arms came from the pre-plan board and the pre-pass
ran before any cast, so a creature the plan casts first (sick = a free body) could never convoke -- unfindable at
any budget. Fix: arms may count arrivals (`Action::convoke_plan_pip / _other`; every split between arrivals and
would-attack board bodies is its own arm), `ApplyConvokeArrivalDependencyOrder` (end of both
ApplyPayableCastOrder forms -- every apply site in both worlds) casts such an arm after its arrivals, rule 7
requires them, and the human ordering search drops an order that does not (`OrderingPlacesConvokeBeforeArrivals`).
Tests: "Convoke B" x2 (Dire Moon then Baron convoked by it on three Swamps, both worlds; the dependency order).
Repro: 91016 T5 menu offers "Falkenrath Pit Fighter, Markov Baron (convoke: Legion Lieutenant #24, Asylum
Visitor #5, Falkenrath Pit Fighter #21)"; 91005 handed to the search at d5 b200 now plays T3 "Condemned,
Lieutenant, Baron convoked by Condemned + Lieutenant + Gorger" (4 to the face vs 3; both arms win T4, opp -12
vs -6). The sweep's "6 on T3" line needs mana the board does not have; not reproduced.

**C -- arm order ignored lords and the pip colour.** Root cause: `ClassifyConvokeBodiesEx` sorted would-attack
bodies by `EffectivePower()` (no lord term) and only PREFIX arms of that list existed. Fix: creature rules rank by
`CombatPowerOf` (`ConvokeRules::true_power`) and the arms are the full (pip, other) grid, one per (reduced cost,
plan need) keeping the least attack power given up (ties: fewer costly, fewer pip bodies); the USER's question-5
default (lowest power first) now applies to TRUE power. Test: "Convoke C" (both "tap the black Baron, pays {B},
loses 2" and "tap the lord-pumped Gorger, pays {1}, loses 3" are offered; costly order by true power). Repro: 91000
T5 offers "Drana, Markov Baron (convoke: Drana #15, Markov Baron #28)" -- the Gorger stays attacking.

**D -- convoke arms printed identically.** Fix: every creature-convoke cast's summary and JSON (`"convoke": {pip,
other, plan_bodies, taps}`) name the bodies it REALLY taps, off a probe apply with a depth-gated recorder
(`g_convoke_tap_record`; main.cpp `ConvokeLabelsFor`) -- a Bloodghast the plan's land returns and a creature the
plan casts first are named; the convoke event and the madness chooser labels name bodies too; human-menu twins
that realise the same bodies (board arm vs plan-body arm once the arrival is cast first) are hidden in the capped
menu. Test: "Sweep D" (the recorder names "Vampire of the Dire Moon #10"). Repro: the 91011 / 91016 menus above.

**E -- outlets whose discard the plan's own land / casts empty.** Root cause: human play emits the discard outlets
UNPINNED, and rule 1 counted pinned discards only; the executor then skipped the pump silently. Fix: subset rule
8 (`SubsetDiscardOutletsUnpayable`, both walkers): every discard outlet, pinned or not, needs a card at its turn in
the EXECUTED trailing order (OrderTrailingActivations), net of the selection's casts / puts and the earlier
outlets' draws (the land drop is already off the walker's board -- EnumeratePlansWithLand plays it first -- so the
coordinator's land-drop note needs no extra term). The search never credits a dead pump (the trailing pass
checks liveness first). Test: "Sweep E" (pump + crack on one card rejected; a cast that empties the hand
rejected; both accepted with a second card; the executed order). Repro: 91009 / 91011 / 91013 / 91007 -- the
phantom plan has NO match in the new menu at that decision; all-or-nothing on those menus: 6 + 24 + 46 + 2 plans,
0 partial.

**F / L -- the payer (sub-integrator, `6939bd4d3`).** Root cause: Voldaren Estate, Unclaimed Territory and
Secluded Courtyard tie at rank 59, so battlefield order picked; the backtracker returned its first DFS hit, and the
painland {C}-first hoist only covered generic pips. Fix: `MTG_PAY_PAIN_LAST` (a life-costing coloured mode is the
last resort for coloured pips too, in the greedy, the backtracker -- a painless solve first, the old solve as the
fallback -- and the prepay shrink) and `MTG_PAY_NARROW_RESTRICTED_FIRST` (a creature-spell-only colour before a
Courtyard), both default ON, heurarm, lossless; armed Prevent Damage boards keep their pain. Tests:
`test_vampires_payer.cpp` (15 cases, 8 fail with the levers =0). Repros (life before -> after): 91017 19->20,
91012 20->21, 91018 17->18, 91013 18->19, 91007 17/20 -> 18/20, 91004 17/19 -> 18/19, 91016 plans 48/52 19 -> 20;
91001 now taps the Territory for the cast and keeps the Courtyard for Pit Fighter's {1}{R}. **F x L follow-up**
(the sub-integrator's open question 1): pain-last sent a creature SPELL's pip to the Courtyard and Pit Fighter's
own {R} (a creature ABILITY: Territory / Estate colour cannot pay it) no-op'd. `PlanTraits::act_pips_cab` now
carries Pit Fighter's and the transform's pips and `ActLineHoldMask` reserves an ABILITY-LEGAL provider for them;
both trailing passes bracket Pit Fighter's payment in `ActLinePayScope`. Test: "payer F x L (search)".

**G -- duplicate Blood plans for identical Estates.** Fix: TapBlood actions carry the interchangeable-source
`equiv_tag` (the k variant rides the tag), so the canonical-prefix fold drops the twin wherever it applies (the
greedy walk always; the search walk under the deck opt-in, `FoldSearchOdometerOptIn`, which Vampires does not
take -- open question 33); the human menu (never folded at enumeration) hides the twin. Test: "Sweep G" (with the
fold arm pinned on, single-Blood plans use one Estate; before: both).

**H -- transform missing from human plans that reach five.** Root cause: human play emitted the transform only
when the restriction already held on the pre-plan board. Fix: human play takes the search's gate (the plan can
reach five; rule 6b checks arrivals), board Keeper and a same-plan Keeper alike. Repro: 91012 T5 now offers
"Territory; Aether Vial, Legion Lieutenant, Bloodline Keeper: transform it" and it executes (Lord of Lineage).

**I / all-or-nothing -- a transform only Territory could pay; a pump that ran while its transform died.** Fix (human
menu): a plan holding a Vampires activation (steal / transform / Blood / the discard outlets) is probed on a copy
(land fixed as the frame decided it) and dropped when its apply SKIPS any activation (`g_trailing_act_skips`,
counted at the trailing pass's not-live / unpaid branches); the same plan without the dead activation is its own
entry. Repro: 91012 T5 with only Territory untapped offers no transform; 91004 T8 offers the transform only with
the Swamp land drop (all-or-nothing on that menu: 52 plans, 0 partial). The AUTONOMOUS creature-ability pool
still counts a colored_creature_only land as any colour (the USER withdrew MTG_CCO_NONCREATURE_POOL as
enumeration hygiene that must earn its place; the narrow creature-ability analogue is the same class) -- open
question 32.

**J -- listed order vs executed order.** Root cause: the menu listed the plan vector; the trailing pass runs in
OrderTrailingActivations order (VampiresProvider's draft: Blood creation -> Condemned -> crack -> Pit Fighter).
Pricing parity holds: the Blood variant counts only casts / puts (rule 3) and runs first in both worlds, so it is
priced and realised at the same {1}. Fix: a Vampires plan lists its trailing activations in the executed order
(main.cpp `DisplayActionOrder`, summary + JSON). Repro: 91015 T5 now reads "Voldaren Estate: create a Blood
token, Stromkirk Condemned: Vampires +1/+1". The ORDER itself is open question 34.

**K -- the batch prepay ignored convoke.** Root cause: BatchPrepayMainCasts' combined bill recomputed each cast
from EffectiveCost and never applied the convoke reduction (Chord never reached it: X spells decline). Fix: the
bill applies the arm's reduction (the bodies tap at the cast; rule 7 makes the bill match). Test: "Convoke K"
(Pit Fighter + a 3-body Baron: one land, nothing floating, the Estate untapped, in both worlds). Repro: 91016 T5
"Pit Fighter, Baron (3 bodies), Estate: Blood" taps one land for {R} and makes the Blood (life 20).

**Verification.** `mtg-test` 1540/1540 (1514 + convoke 7 + sweep 4 + payer 15). The enumeration-level convoke
cases (A conservation, B found, K one land, C the black-Baron arm) were also built against the PRE-fix tree
(1c4484e7e, a scratch worktree, removed): all four FAIL there (A: 6 mana cast off 3 bodies and 0 lands; B: no such
plan; K: 4 lands tapped, {3} floating, life 19, the Estate tapped; C: no such arm). The rules-7/8 / recorder /
fold cases drive predicates that did not exist before. **Smoke** (`regression.sh
--smoke`, NOT accepted), my commit alone: 128 pass / 4 fail -- exactly the four Vampires rows, every other deck
byte-identical (Melira Pod included). With F/L + the F x L hold: 106 / 26; Vampires
`vampires_smoke_d0` 4.8300 -> 4.7000, `_d3` 4.4667 -> 4.3800, `_d5` 4.4267 -> 4.3467, `vampires2hg_d3` 5.0400 ->
4.8800 (searched: 0 slower, 27 faster; d0: 17 slower, 147 faster -- the slower d0 games checked are the removed
phantom: e.g. gi12 T3 old "Baron, Baron" on two lands + three bodies). Non-Vampires: 153 moved games, every one
reproduced as a payer-lever move from a single-game repro (a pooled batch of the 153 with both levers =0
reproduces the committed GT digest 153/153; mechanism per game from a levers-off vs -on log diff): 117 a coloured
pip paid without pain (life +1), 12 a different (painless / narrower) land tapped, 6 a same-name copy swap, 15 an
extra cast the saved colour funds, 3 a different searched line at the same score; score moves only at d0 --
faster: treasure_hunt gi146 8->7, gi450 6->5, minotaur gi444 6->5 (Territories pay the cast, the Courtyard funds
Burning-Fist's {1}{R}), pirates gi530 5->4 (Fiery Islet's {R} left for Lightning Bolt); slower: soldiers gi49 5->6,
gi817 4->5 (the payment is painless and keeps Silent Clearing; the d0 runner then sacrifice-cycles the Clearing).
Searched non-Vampires moves: 37 games, all at the same score. **Sanity** (`--batch`, MTG_SLOW_GAME_MS=10000):
d3 b20 40 games seed 777 avg **4.3750** (was 4.4250), 5.56 s wall, 1,011 core-ms/game; d5 b40 20 games avg
**4.3500** (was 4.4000), 6.05 s wall, 1,317 core-ms/game; no game over 10 s.

## Open questions -- sweep fixes (work continues on the stated default)
32. **Autonomous creature-ability pool**: a colored_creature_only land still counts as any colour for a creature
    ABILITY's pip in the search (the transform / Pit Fighter variants the payer then cannot fund are rollout
    duplicates, never wrong lines). Same class as the USER-withdrawn MTG_CCO_NONCREATURE_POOL. [default: unchanged;
    the human menu's all-or-nothing probe removes the phantom there]
33. **Vampires FoldSearchOdometerOptIn** (the canonical-prefix fold on the search's own walk -- identical Estates,
    identical hand casts): per-deck, verified-before-adoption. [default: not taken]
34. **The Vampires trailing order** (AGENT-DRAFTED, question 11) runs Blood creation BEFORE the discard outlets, so a
    madness-cast Vampire from Condemned / the crack never discounts the Blood (91015), and "crack first, then pump the
    drawn card" is reachable only through the crack's hand-entry breakpoint. Options: Blood after Condemned / the
    crack, or search the order. [default: unchanged -- the order is yours]
35. **Plan-body convoke arms in rollout leaves**: beyond the horizon only arm 0 (free board bodies) is offered, as
    for every other convoke variant (question 15's playout policy). [default: on]
36. **Payer over-tap with a held activation pip** (observed, not caused here): 91012 T5 "Vial, Lieutenant, transform"
    taps five lands for four mana ({C} floats) -- the prepay ladder under MTG_ACT_LINE_HOLD. Payment quality only.
    [default: disclose; follow-up]
37. **The payer sub-integrator's open questions** (in `6939bd4d3`): MTG_HUMAN_PREPAY_DEMAND still outranks pain-last
    (91016 plan 50 costs a life in human play only); the backtracker's needless first-candidate taps; recorded
    `--tap-pref` taps still win over the new order in reference replays. [defaults: as shipped]

## Payer A/B -- the slower games root-caused (2026-10-18, branch `vampires-payer2`, NOT pushed)
The pooled held-out A/B (`test/tools/payer_pain_ab.py`, 8 decks x {std d0, d3 b10, d5 b20, 2hg d3 b10} x blocks
A/B, both levers on vs =0, 81,600 games) left six slower games and two decks net slower (vampires +1,
creature_giving +1). Per game, from a levers-on vs -off single-game log diff (MTG_TRACE_SOLVE /
MTG_FSW_TRACE / the new MTG_TRACE_SOLVE_BOARD):

- **vampires gi129 block B (d3 b10 and d5 b20, T5 -> T6) -- DEFECT, fixed.** The budgeted root skipped its T1
  land drop. Pass 1's horizon leaf (the 1-ply SimulateToEnd) under "T1 Voldaren Estate" played T4 "Courtyard,
  Pit Fighter" and paid the {R} with Secluded Courtyard (painless) instead of an Estate's pay-1-life colour; main
  2's Pit Fighter draw-two ({1}{R}, a creature ABILITY -- only the Courtyard's colour or an unrestricted {R} may
  pay it) then had no legal {R}, the leaf's kill slipped T5 -> T7, the T1 roots scored Estate 7 / no-land 6, and
  the 8x costlier pass 1 (7,987 units vs 963) left no budget for pass 2. Unbudgeted (d3 b0, d8 b0) both arms win
  T5. Sweep defect L one phase later: the activation-line hold (`act_pips_cab`) sees an activation only inside the
  plan, and Pit Fighter's lost-life restriction makes its activation a main-2 plan. Fix: a main-1 plan that
  attacks with such an outlet on board or among its casts owes the outlet's pips as creature-ability demand
  (`ComputePlanTraits`, NARROW-gated; Pit Fighter is the only card it matches). Now T5 at both budgets.
- **creature_giving gi24 block B (d0, T4 -> T5) -- DEFECT, fixed.** Pain-last tapped the Forbidden Orchards for
  Birds' and Crop Rotation's {G}; the additional cost (tapped-first, Orchard last) then sacrificed the UNTAPPED
  City of Brass and the plan's Sylvan Scrying (payable by the walk, which never models the sacrifice) had one
  land. Fix: `SacLandPayVictim` (SpellEffects.h) publishes the land the cost will take as the sac-fodder victim
  around the cast's payment, both worlds (pain-last-gated). Its first build promoted the ranking's FRONT and
  sacrificed a Stomping Ground over a Forest (gi1565 block A, T7 -> T8, a new slower game in the re-run); it now
  promotes the LEAST flexible best-band land that can pay a pip of the cost -- what the scarcity payer would have
  tapped from that band. Burn (Shard Volley, all Mountains) is byte-identical.
- **th gi1155 block A (d0, T6 -> T9 = a loss) -- legitimate.** The painless order routes both Fiery Islet taps
  through the Cascade Bluffs, so T5 pays TWO Treasure Hunts for the 2 life the old order spent on ONE (both
  Islets for a single {1}{U}); the d0 runner casts the second, it draws the rest of the library (no nonland card
  left), and the T6 draw decks it. Off could not afford the second Hunt, cast it T6 with Land's Edge and won.
  The payment is strictly better; the deck-out is the d0 policy's (open question 38).
- **minotaur gi83 block A (d0, T5 -> T6) -- legitimate.** Narrow-first pays Slaughter-Priest with the two
  Territories and keeps the Courtyard + Blood Crypt, so the d0 runner can now pump Burning-Fist on T4; it
  discards Bloodstained Mire for it, and T5 then has neither the fetch's Slaughter-Priest sacrifice trigger
  (+2/+0) nor a card for the T5 pump -- 2 short of off's T5 kill. Same total pump damage; the discard choice and
  the timing are the d0 policy's.
- **soldiers gi1554 block B (d0, T5 -> T6) -- legitimate** (the probe's gi49/817 class). Champion's {W} comes
  off a Territory (painless) instead of Silent Clearing (1 life); the d0 runner then sacrifice-cycles the
  untapped Clearing with the other Territory, is a land short on T4 and cannot activate Recruitment Officer.

**Re-run** (same manifest, final binary, one pooled batch, 22 threads): per-deck net vampires -1, auras 0,
creature_giving -3, minotaur -2, pirates -2, selesnya 0, soldiers 0 (d0 +1, 2hg -1), th -4; 0 slower in any
searched cell; slower games: th gi1155, minotaur gi83, soldiers gi1554 (the three d0 games above). The =0 arm is
byte-identical to the first run (64/64 wins files, digests included). Tests: `test_vampires_payer.cpp` "payer
A/B ..." (gi129 both worlds + scoping; gi24 both worlds and both arms; the predictor's band / tapped / cost
rules; gi1565's least-flexible pick).

38. **A flood engine that empties the library** (th gi1155): the d0 runner casts a DrawUntilNonland Treasure
    Hunt when the library holds no nonland card and no same-turn kill, and decks itself. A provider prune
    (TreasureHuntProvider::ShouldCastDrawEngine) would be the place. [default: unchanged -- d0 policy, yours]

## Open questions -- Stage 5f search cost + sweep 2 (work continues on the stated default)
39. **Subset rule 9 (`MTG_VAMP_M2_PUMP_DISCARD_FOLD`) is a DOMINANCE fold shipped ON.** Its deck half rests on a
    reading of this list: after combat nothing reads power, and nothing reads the graveyard or a discard except
    Bloodghast and madness (both exempted, as are an emptied hand with Asylum Visitor out, > 7 cards at cleanup,
    and a plan that draws). Sign-off? [default: ON]
40. **Breakpoint CHAIN arm off for Vampires** (the per-deck setting five other decks use): -1.1% to -2.2% units,
    -8.1% on 2HG gi34, win turns identical. [default: unchanged (on)]
41. **A general EMPTY-arm fold** (skip a breakpoint's "cast nothing more" arm when it repeats its base plan; 100% of
    gi34's empty arms did): ~8% on gi34, ~2% on the 2HG cell; touches breakpoint code every deck uses and moves
    every deck's GT. [default: not built]
42. **The search-walk canonical-prefix fold** is still question 33: -0.1% to -0.4% here, byte-identical; needs its
    verification run. [default: not taken]
43. **The Vampires trailing order hides two cheaper-Blood lines** (extends question 34). The AGENT-DRAFTED order runs
    Blood before Condemned's pump and the Blood crack, and the count band runs the steal after them, so "pump
    discarding a madness Vampire, then a cheaper Blood" and "steal, then a cheaper Blood" cannot be expressed
    within one main phase (only the main-2 Blood substitutes). Candidate amendment: token taps -> pump -> crack ->
    steal -> Blood -> transform -> sacrifices (risk: a madness cast paying first could tap the Estate for mana;
    the activation hold reserves it first). [default: unchanged -- the order is yours; not measured]
44. **The 5c2 48-block re-run** (the script's own suggestion after NO SIGN; ~96k d5 games, ~4-5 h). Not run: at
    0 / 12,000 the larger sample could only confirm the lever does not bind. [default: keep ON, not re-run]
45. **ANSWERED 2026-10-20 (USER): (b), and the gate is HARD.** *"No, the suite entrance is a hard rule for a
    reason. It means the deck needs more work. However, that doesn't prevent us from pushing the code so it is
    somewhere rather than local, as long as the changes don't break another deck."* No `MTG_ALLOW_UNTESTED_DECK`.
    The code is landed on the shared branch WITHOUT the suite rows (they are kept as
    `docs/design/analysis-Vampire.suite-rows.patch`; see "Landing" below); getting under 3x is the first goal,
    ahead of both generators. Original question: **COST GATE: Vampires is over 3x** (see "Stage 5 measurements + the 3x COST GATE"). Options: (a) land the rows
    (an over-3x entry, USER override) and generate the mulligan profile, whose keep table removes the 68-76% of units
    that are bottoming rollouts (measured: then ~1.6x Snow); (b) keep the deck out of the suite until a perf route
    gets it under 3x first (no keep table is allowed before suite membership, so (b) needs a non-mulligan route --
    the remaining folds are questions 40-42, each a few %). [default: neither -- rows stay on the branch, nothing
    landed, no generator started; I would recommend (a)]
46. **The cost cache's Galadriel reference is stale** (2,969 vs ~94 ms/game today), so the 3x gate is mis-calibrated
    for every deck. [default: refill with `suite_gate.py --measure-all --no-run` off the next full regression run
    and commit `test/suite_cost.json` with this branch] -- DONE 10-19 (b50e1c69d: Galadriel 2,969 -> 75, reference
    hinata 1,468). The 10-20 landing run was contended and NOT used; a clean refill on a quiet box is still owed.
47. **Bucket amendments from the d0 root-cause** (cast-order doc 5.1): (1) Keeper's delayed value x0.75 (Keeper 12 ->
    10.5); (2) duplicate copies priced at cumulative cost. Each changes searched play too, so each needs the same A/B.
    [default: buckets as drafted, OFF; amendments not tried]
48. **Depth-0 runner blind spots** (hit both arms equally; they make d0 a noisy test for any discard / order rule):
    the lethal check ignores a lord cast this turn, Condemned's pump and Keeper's transform; convoke tapping attackers
    is not counted as lost attack; the Blood's +1 breaks ties against casting a lord; a colour-infeasible madness
    cast scored as payable (2343, no effect). The d0 runner is the permitted greedy, so this is a quality work item,
    not a no-greedy defect. [default: not taken up]
49. **The trailing order is a fixed sequence per plan, not ordering-only** (the Stage 6a audit; the provider class
    header calls it ordering). Beyond questions 34 / 43, three more lines are inexpressible within one main: Keeper
    token -> a hand-cast Baron convoking it (casts run before activations), steal -> Baron convoking the stolen body,
    steal -> Pit Fighter sacrificing the stolen creature. Main 2 or a breakpoint recovers some. [default: disclosed]
50. **Question 4's cleanup-madness "always cast" argument misses Asylum Visitor**: casting the last card at cleanup
    empties the hand, and the Visitor's upkeep draw costs 1 life, lethal at 1 life under own-death checks.
    Theoretical. [default: unchanged]
51. **Rollout madness pin** (D1 fix): beyond the horizon the single madness variant is now the default cover chosen
    on the pre-plan board and realised with the cheapest bodies at the trigger (it no longer uses free bodies the
    plan adds first). Alternative: compute the rollout's pin on a board including those arrivals. [default: as built]
52. **GUI: Markov Baron's hard-cast convoke arm cannot be picked** (`CheckLine` has no convoke dimension, so arms with
    the same casts collapse to the first payable one; claude-play lists every arm). [default: viewer follow-up]
53. **Viewer auditor coverage**: `sacrifice` is never an expected decision type (Pit Fighter's `sac_creature_outlet`
    is classed INERT), so the dynamic sweep never checks that prompt (the claude-play sweeps did, by hand); and the
    INERT rows for Asylum Visitor and cluster C's combat params await your OK (question 31). [default: as classified]

<!-- verify_deck:begin (generated -- do not edit inside) -->
## Last verification (2026-10-19)

`verify_deck.py decks/Vampires/Vampire.cod --write-ledger` -> **PASS**

| Gate | Status | Blocking | Summary |
|---|---|---|---|
| coverage | PASS | yes | all 20 cards full (missing=0, partial=0) |
| card_costs | PASS | yes | all mana costs match Scryfall (cost/cmc only) |
| card_fields | SKIP | yes | snapshot incomplete (1 unfetched) |
| clause_ledger | SKIP | no | covered by coverage+bracket-notes+oracle-diff |
| regression_tiers | PASS | yes | in all 3 suite tiers with GT (vampires) |
| viewer | PASS | yes | self-guard + surface sweep clean |
| viewer_wiring | PASS | yes | 6 type(s) wired (emitter + GUI): dig, discard, madness, revive, target, vial_charge |
| mismatch | PASS | yes | no nonconv/fd-diverge across seeds [7001, 8001] x 60 games (both arms completed) |
| play_invariants | PASS | yes | 8 game(s)/225 decisions: determinism+integrity+progress hold |
| claude_sweep | PASS | yes | Claude-play sweep recorded, 0 unresolved flags |
| suite | PASS | yes | suite gate: Vampire is regression key `vampires` |

### Pending user sign-off (block the gate until fixed OR approved below)
_none_ -- every blocking gate is green or already signed off.

### Stage 6a disclosure (deferrals + not-yet-built checks)
- coverage deferral -- Falkenrath Pit Fighter: Repeatable creature-sac VALUE outlet with a DISCARD additional cost (sac_creature_outlet + sac_creature_requires_subtype Vampire -- itself legal -- + sac_outlet_discard_cost 1 + sac_outlet_draw 2), applied by its own shared resolver ApplyDiscardSacOutlet in both worlds. WHICH card is discarded and WHICH Vampire dies are both SEARCHED: one variant per distinct hand-card name x per identity-folded victim class on the board, plus a victim the SAME plan casts or Vial-puts first (one per Vampire creature name in hand); every variant shares the pooled source's activation family, so one activation per plan, and a further activation is a fresh searched decision reached through the two-card draw's hand-entry breakpoint. Human play gets one entry and is asked both at resolution (the `discard`, then the `sacrifice` decision). COST ORDER: mana -> discard -> sacrifice -> [madness seam
- coverage deferral -- Falkenrath Gorger: grants_madness_creature_subtype: a STATIC grant read at the moment of each discard by MadnessCostsFor -- while a Gorger we control is on the battlefield, every discarded creature card with subtype Vampire has madness for its own printed mana cost (front face). The option is SNAPSHOTTED into the pending madness record, so a Gorger that leaves between the discard and the trigger's resolution (e.g. sacrificed to Falkenrath Pit Fighter AFTER the discard -- costs are paid discard-first) still lets the card be cast (2016-04-08 ruling). A discarded Gorger gets madness only from ANOTHER Gorger on the battlefield. Two Gorgers grant one instance; a card with its own equal-cost madness (Markov Baron, Asylum Visitor) has one option. 'Cards you own that aren't on the battlefield' is read at the only place a card can be discarded -- our hand -- so the grant is evaluated exactly where it can matter. Every discard site routes through the shared DiscardCardFromHand hook; the cast-or-decline is a SEARCHED decision on an outlet discard (Action madness pin) and the provider's PROVISIONAL 'cast if payable' dominance rule at cleanup (USER question) -- see the madness core in SpellEffects.h. The madness cast is a real cast (spells-cast count, on-cast triggers) that enters through the shared enter cascade; it does not traverse the stack (no cast-trigger card can be madness-cast in this pool).
- coverage deferral -- Legion Lieutenant: Lord: lord_effect + subtypes_affected ["Vampire"
- coverage deferral -- Stromkirk Captain: Lord half: lord_effect + subtypes_affected ["Vampire"
- coverage deferral -- Stromkirk Condemned: Searched main-phase Action::Kind::ActivatePump mode 4 (discard-cost team pump): no mana and no {T}, so it is legal the turn Condemned is cast (reached in the same main through breakpoint site 9). WHICH card is discarded is SEARCHED (one variant per distinct hand-card name; human play: the `discard` decision at resolution), through the shared discard hook, so a madness Vampire discarded to it is cast and is on the battlefield before the pump resolves (2016 ruling) and gets +1/+1. The +1/+1 lands on every Vampire we control AS THE ABILITY RESOLVES (temp P/T, cleared at cleanup); later arrivals get nothing. Once-per-turn is per copy (Permanent::once_per_turn_used, reset at our untap in both worlds); two identical copies are folded to their canonical discard assignment. Deliberately NOT team_pump_cost, so the combat firebreathing converter can never spend it. PROVISIONAL deferral: instant-speed timing collapsed to the main phases' trailing passes (a combat activation buys nothing a main-1 one does not vs the passive opponent).
- coverage deferral -- Captivating Vampire: Lord: lord_effect + subtypes_affected ["Vampire"
- coverage deferral -- Bloodghast: LANDFALL FROM THE GRAVEYARD (landfall_return_self_from_gy): a GRAVEYARD watcher (CardDefinition::gy_landfall_watcher) dispatched by the one landfall chokepoint (FireLandfallTriggers) after the battlefield watchers; it functions only in the graveyard (CR 113.6k) and only for a copy already there when the land enters (2009 ruling); the return goes through PutCardOntoBattlefield (untapped, a new object, both enter cascades). The 'may' is resolved YES by a dominance argument (PROVISIONAL, see the A2 design: an extra untapped 2/1 Vampire against a passive opponent is never worse -- nothing in the list reads our graveyard except this card); human play is asked (the `revive` decision). The deck stamps deck_has_landfall and a graveyard-reader bit (GyR_GyLandfallNames, folded into the state keys), so the searched land position is live: a plan whose discard / sacrifice puts Bloodghast in the graveyard is offered 'land after the activations' (it returns), and in the second main a held land is exempt from the newly-arrived-lands-only rule while a Bloodghast is (or can be put) in the graveyard (MTG_VAMP_GY_LANDFALL_HELD_LAND, a USER question). CONDITIONAL HASTE (haste_if_opp_life_at_most 10): a derived Permanent::cond_haste bit, read against the opponent's life (team life in 2HG) at its entry and at the beginning of combat in both worlds. [PARTIAL: can't block is inert -- the passive opponent never attacks, so we never block. PROVISIONAL deferral pending user sign-off.
- coverage deferral -- Voldaren Estate: MANA: produces all five colours plus {C}; tap_self_damage 1 with a {C} mode = the painland reading, so the {C} tap is free and each coloured tap costs 1 life (raw life loss -- 'pay', not damage). colored_creature_only restricts the coloured half to creature SPELLS (madness and convoke casts included), never an activated ability. 'Vampire spell' is simplified to 'creature spell': EXACT for the Vampires list (every creature is a Vampire -- type lines verified; the only noncreature spell, Aether Vial, costs generic {1} which the {C} pays identically). BLOOD: PermAbilityMode::TapBlood, cost {5} minus one generic per Vampire we control AT ACTIVATION (floor {0}; the {T} stays) -- the search prices one cost variant per number of Vampires the plan casts first, and both worlds pay the true cost when the trailing pass activates it -- creating the 'Blood Token' named token def; the Estate taps itself for the {T}, so it can never pay its own {5}. A searched activation (when/whether); a Blood made this turn is crackable this turn and its crack is offered through breakpoint site 9.
- coverage deferral -- Unclaimed Territory: Modelled as producing all five colours plus {C}. colored_creature_only enforces the restriction in the payer (ProducesForPayment / TapFlowInfeasible / the backtracking payer): for a noncreature spell, or any activated ability, this land yields only {C}. So it cannot pay Lightning Bolt's {R} or Siren Stormtamer's {U}, but it pays generic costs such as Aether Vial {1} and a Clue's {2}. The ETB creature-type choice is simplified to 'any creature spell'. That is outcome-exact for Pirates: every creature except Metallic Mimic {2} and Adaptive Automaton {3} is a Pirate, and those two cost only generic mana, which the land's unrestricted {C} pays identically. Search-side only: with MTG_CCO_NONCREATURE_POOL off (default) the noncreature enumeration pool counts this land as any-colour, so a Bolt plan relying on it can be offered and is then refused by the payer (no illegal play).
- coverage deferral -- Unclaimed Territory: Soldiers (2026-10-04): EXACT there too -- every creature in that list is BOTH a Human and a Soldier (Scryfall type lines verified), so naming either type pays for every creature it casts; King Darien XLVIII's {G}/{1}{G}{W} and Harbin's {U} are castable ONLY through these lands (Harbin also off Fortified Beachhead).
- coverage deferral -- Unclaimed Territory: Vampires (2026-10-18): EXACT there too -- every creature card in that list is a Vampire (Scryfall type lines verified for all 14 creature cards, both faces of Bloodline Keeper included), so naming Vampire pays for every creature spell it casts, madness casts included; its coloured mana is correctly unusable for the list's creature abilities (Falkenrath Pit Fighter's {1}{R}, Bloodline Keeper's {B} transform), which this land's oracle does not cover.
- coverage deferral -- Aether Vial: AI policy. CHARGE: the shared hand-aware rule WantVialCharge (executor and rollout alike) -- HOLD while an undeployed creature in hand has mana value equal to the counters (from two counters up; it climbs instead when the next level also holds a creature, unless this level holds two or more extra), else CLIMB while a creature in hand has a higher mana value, else pre-charge toward the deck's charge target (vial_target_mv: the most common creature mana value in the list, ties going up). The charge is not a searched decision (the searched charge axis MTG_VIAL_AXIS ships OFF, USER ruling 2026-08-30). PUT: searched -- one main-phase action per creature card in hand whose mana value equals the counters, both mains.
- coverage deferral -- Markov Baron: All four clauses modelled. LORD: lord_effect, subtypes_affected Vampire, +1/+1, lord_excludes_self (Knight Exemplar shape; Vampire tokens included). LIFELINK: the keyword, read by CreatureHasLifelink at the combat-damage site (our life total is real: Voldaren Estate's pay-1-life and Asylum Visitor's loss draw on it, and own-death checks are live for this deck). CONVOKE: the Chord of Calling cost-reduction model GENERALIZED to a creature spell and to the spell's coloured pip (a BLACK body pays {B} or {1}, any other body {1}); summoning sickness irrelevant; SEARCHED arms = max FREE bodies (cannot attack / 0 power, after combat every untapped body), then free + k COSTLY bodies (a would-attack body pre-combat, lowest power first -- USER question; or one with a live non-mana {T} ability); the no-convoke cast is dropped only while a free body exists and nothing else this turn claims the bodies (weakly dominated: a free body's tap has no other use against the passive opponent; a second convoke card or Captivating Vampire's steal is a claimant, and then every fewer-free arm down to the plain cast is offered). Applies to the hard cast AND to the madness cast (convoke applies after the alternative cost). A searched madness cast is pinned by WHAT its bodies pay (pip and generic covered), realised on the board at the trigger by the cheapest bodies there -- so a body the plan adds first (a cast, a Vial put, a Bloodline Keeper token, an earlier madness cast) can pay it, and the mana booked is the mana paid. MADNESS {2}{B}: own madness_cost; with a Falkenrath Gorger out the granted instance has the same cost, so one option. The cast-or-decline is searched (outlet Action pin) or the provider's cleanup dominance rule.
- coverage deferral -- Asylum Visitor: Both clauses modelled. UPKEEP TRIGGER (hellbent_upkeep_draw / _life_loss / _each_player): OUR upkeep -- PerformUpkeepHellbentDraw, lockstep in GameEngine::UpkeepStep and SimulateEndAndStartNextTurn, ordered FIRST among our upkeep triggers; the intervening-if is evaluated on NON-STAGED hand cards and re-checked on resolution, so two Visitors give ONE draw (2016-04-08 ruling). Draw = a real CR 121 draw (cards_drawn_this_turn; an empty library loses as at the draw step), loss = life loss (not damage). OPPONENT's upkeep -- PerformOpponentUpkeepHellbentDraw at the turn boundary just before the opponent's notional draw; it reads the opponent's real hand only when one was dealt (opponent_library_dealt); otherwise the passive opponent holds its untouched opening seven forever and the condition is false. When it fires, every Visitor resolves (our draw does not fill THEIR hand). MADNESS {1}{B}: own madness_cost; with a Falkenrath Gorger out the granted instance has the same cost, so one option.
- coverage deferral -- Secluded Courtyard: Modelled as producing all five colors; the ETB type choice is simplified to "any creature" (exact for the mono-tribal decks that run it). colored_creature_only restricts the coloured mana to creature spells; colored_creature_ability_ok additionally permits it for an ACTIVATED ABILITY whose source is a creature (D12 fix: Burning-Fist / Sethron activations, paid under the CreatureAbilityPayScope guard). Ability sources are simplified to battlefield creatures -- the oracle's any-zone "source" (e.g. cycling a creature card) is not modelled.
- coverage deferral -- Secluded Courtyard: Soldiers (2026-10-04): EXACT there too -- every creature in that list is BOTH a Human and a Soldier (Scryfall type lines verified), so naming either type pays for every creature it casts; King Darien XLVIII's {G}/{1}{G}{W} and Harbin's {U} are castable ONLY through these lands (Harbin also off Fortified Beachhead).
- coverage deferral -- Secluded Courtyard: Soldiers: its coloured mana also pays the abilities of Recruitment Officer ({3}{W}) and King Darien XLVIII ({3}{G}{W} -- its ONLY green source for that ability): every ActivatePermAbility whose source is a creature now pays under CreatureAbilityPayScope at both apply sites, as ActivatePump always did.
- coverage deferral -- Secluded Courtyard: Vampires (2026-10-18): EXACT there too -- every creature card in that list is a Vampire (Scryfall type lines verified for all 14 creature cards, both faces of Bloodline Keeper included), so naming Vampire pays for every creature spell it casts and every creature-source ability it activates (Falkenrath Pit Fighter's {1}{R}, Bloodline Keeper's {B} transform, both paid under CreatureAbilityPayScope).
- coverage deferral -- Blackcleave Cliffs: Fastland: fastland_max_other_lands 2 -- LandWouldEnterTapped counts the lands the active player controls (the land being played is still in hand, so each counts as 'other') and it enters tapped iff that count > 2. Same predicate on the executor drop, the rollout drop and the enumerator's priced land copy. Fully modelled.
- coverage deferral -- Vampire of the Dire Moon: Lifelink: printed keyword, read by CreatureHasLifelink in the shared combat core (executor + rollout) -- a life-gain event per connecting swing; our life total matters to Asylum Visitor's draw-and-lose-1 and Voldaren Estate's pay-1-life, nothing in the list triggers on gaining life. PARTIAL, PROVISIONAL: Deathtouch is parsed (Keyword::Deathtouch) but has no reader -- structurally inert: it only ever deals damage to the opponent's face (the passive opponent never blocks; no fight/bite effect in the list).
- coverage deferral -- Vampire Socialite: CLAUSE 2 (intervening-if ETB, CR 603.4): etb_each_other_own_creature_counters_if_opp_lost_life 1 with etb_counters_subtypes [Vampire
- coverage deferral -- Vampire Socialite: PARTIAL: Menace is inert -- the passive opponent never blocks. PROVISIONAL pending user sign-off.
- coverage deferral -- Vampire Socialite: PARTIAL: CR 614.12 -- a Vampire entering SIMULTANEOUSLY with Socialite does not get the static's counter; the engine enters permanents one at a time, and no card in this deck puts two Vampires onto the battlefield in one event. PROVISIONAL pending user sign-off.
- coverage deferral -- Mavren Fein, Dusk Apostle: Fully modelled, nothing deferred. attack_one_or_more_tokens 1 with attack_one_or_more_subtypes [Vampire
- coverage deferral -- Drana, Liberator of Malakir: FIRST STRIKE + TRIGGER MODELLED (CR 510.4): when an attacker has first or double strike the shared combat core (Combat.cpp ResolveCombatDamageTwoStep, executor + rollout) runs TWO damage steps -- first/double strikers, then the first step's combat-damage triggers, then the creatures without first strike (and double strikers) with power recomputed. Drana strikes first, so her trigger (combat_damage_counters_each_attacker 1: a +1/+1 counter via the doubler-aware PutPlusCounters on every creature still attacking, Drana included; FireCombatDamageTeamCounters) lands before the regular step and every attacker without first strike deals +1 there. First strike is read by CreatureHasFirstStrike (printed keyword + Stromkirk Captain's grant), so under a Captain every Vampire strikes first and the counters add nothing THIS combat (they persist). TurnSolver's PendingAttackDamage carries the exact twin addend (first-step source connects -> per x regular-step attackers; 0 under a Captain). One trigger per damage event, never scaled by opponent heads (2HG: one team, one event). A double-striking Drana would trigger in both steps; the second wave pays off next turn. [PARTIAL: Flying is inert -- the passive opponent never blocks and no card in this deck reads flying. PROVISIONAL pending user sign-off.
- coverage deferral -- Drana, Liberator of Malakir: PARTIAL: no priority window between the two damage steps (an Aether Vial put or a madness cast of Vampire Socialite after Drana connects, before the regular step) -- the engine has no in-combat cast window here; ~<=1% of games (needs Drana + Socialite + the enabler + no Captain). PROVISIONAL pending user sign-off.
- coverage deferral -- Bloodline Keeper: TRANSFORMING DFC, FRONT FACE (Bloodline Keeper // Lord of Lineage); listed under the front name as in every zone but the battlefield (CR 712.8). TOKEN: the tap_token family (Speaker of the Heavens' shape, cost {0}): {T} the source -- summoning sickness applies (TapTokenSourceLive, CR 302.6) -- for a 2/2 BLACK Vampire token with Flying, entering summoning-sick; the colour is load-bearing (a black body pays Markov Baron's {B} by convoke) and every Vampire lord reaches the token. Tapping it means it does not attack: the activation is a searched plan candidate, both mains; a live token tap makes the body a COSTLY convoke body (ConvokeBodyTapAbilityLive). TRANSFORM: PermAbilityMode::TransformSelf -- no {T}, so legal while summoning-sick or tapped; {B} paid as a creature-source ability (Secluded Courtyard's coloured mana may pay it; Unclaimed Territory's and Voldaren Estate's restricted mana may not); the restriction counts Vampire PERMANENTS you control, this one, tokens and a stolen creature included, re-checked on activation (CR 602.5b). Transforming swaps the permanent to the Lord of Lineage entry in place: same object (card number, counters, tapped and summoning-sick status kept), not an entering -- no enter watcher or ETB fires (Vampire Socialite's enters-with-a-counter does not apply). One-shot: the back face has no transform ability. Searched in main 1, main 2 and an IN-COMBAT window after declare-attackers (Plan::combat_transform_choice -> ApplyCombatTransforms), which is the only route when Mavren Fein's attack token is the fifth Vampire; a Keeper cast this turn may transform (no {T}). Trailing-pass order: token taps and the steal before the transform, sacrifices after. Flying: parsed, inert for combat (no blockers). PARTIAL, PROVISIONAL (USER sign-off pending): no opponent-turn window for the token tap (provably equivalent to tapping in our main phase when the Keeper does not attack).
- allowlisted divergence -- Galerider Sliver [keywords]: Keyword-lord: 'Sliver creatures you control have flying' grants flying to your Slivers INCLUDING itself, so the card functionally has flying (modeled 
- allowlisted divergence -- Striking Sliver [keywords]: Keyword-lord: grants first strike to your Slivers incl. itself (modeled self-innate). First strike is inert in goldfishing (no blockers). See oracle b
- allowlisted divergence -- Cloudshredder Sliver [keywords]: Keyword-lord: grants flying+haste to your Slivers incl. itself. Flying self-innate + inert in goldfishing; haste additionally granted to other Slivers
- allowlisted divergence -- Haytham Kenway [keywords]: 'Protection from Assassins' is a real keyword but inert in goldfishing (no Assassins in play); the protection-to-other-Knights is an anthem grant, not
- allowlisted divergence -- Goblin Piledriver [keywords]: 'Protection from blue' is a real keyword but inert in goldfishing (the passive opponent has no blue sources or blockers to target); the attack-trigger
- allowlisted divergence -- Progenitus [keywords]: 'Protection from everything' is a real keyword but inert in goldfishing (the passive opponent never targets, blocks, or damages); the graveyard shuffl
- allowlisted divergence -- Bloom Tender [keywords]: Scryfall lists 'vivid' in keywords -- a data quirk (no rules-meaningful innate keyword on this card); the each-color-among-permanents mana ability is 
- allowlisted divergence -- Auriok Champion [keywords]: 'Protection from black and from red' is a real keyword but inert in goldfishing on all four DEBT axes (the passive opponent never damages, targets, bl
- allowlisted divergence -- Brightcap Badger [mana_cost]: ADVENTURE CARD (CR 715), authored as TWO entries -- 'Brightcap Badger' (the {3}{G} 3/4 creature face) and 'Fungus Frolic' (the {2}{G} instant half). S
- allowlisted divergence -- Fungus Frolic [mana_cost]: The ADVENTURE HALF of Brightcap Badger // Fungus Frolic -- see the Brightcap Badger entry. Scryfall's combined mana_cost is '{3}{G} // {2}{G}'; the in
- allowlisted divergence -- Fungus Frolic [P/T]: An Instant has no power or toughness. Scryfall reports 3/4 because it describes the COMBINED card, whose creature face is the 3/4 Brightcap Badger. Gi
- allowlisted divergence -- Mycoloth [keywords]: 'Devour 2' is a real keyword and is FULLY MODELLED -- but via the `devour` CardParam, not via Keyword::Devour. The engine's Keyword enum deliberately 
- allowlisted divergence -- Brutal Cathar [keywords]: Transforming DFC (Brutal Cathar // Moonrage Brute): Scryfall reports CARD-LEVEL keywords for both faces. The front face's only keyword is Daybound, mo
- allowlisted divergence -- Moonrage Brute [keywords]: Transforming DFC back face: Scryfall's card-level keywords include the front's Daybound and the layout's Transform; Nightbound is modelled structurall
- allowlisted divergence -- Bloodline Keeper [keywords]: Transforming DFC (Bloodline Keeper // Lord of Lineage): Scryfall reports CARD-LEVEL keywords for both faces (Flying, Transform). Transform is the layo
- allowlisted divergence -- Lord of Lineage [keywords]: Transforming DFC back face: Scryfall's card-level keywords include the layout's Transform; the back face carries no transform of its own (transform_fr
- oracle_text advisory -- Gideon, Ally of Zendikar: oracle_text diverges (similarity 0.10); scryfall='+1: Until end of turn, Gideon becomes a 5/5 Human Soldier Ally creature with indestructible that\'s still a pl
- oracle_text advisory -- Basri, Tomorrow's Champion: oracle_text diverges (similarity 0.15); scryfall="{W}, {T}, Exert Basri: Create a 1/1 white Cat creature token with lifelink. (An exerted creature won't untap d
- oracle_text advisory -- Light Up the Stage: oracle_text diverges (similarity 0.69); scryfall='Spectacle {R} (You may cast this spell for its spectacle cost rather than its mana cost if an opponent lost li
- oracle_text advisory -- Crystalline Sliver: oracle_text diverges (similarity 0.61); scryfall="All Slivers have shroud. (They can't be the targets of spells or abilities.)"
- oracle_text advisory -- Galerider Sliver: oracle_text diverges (similarity 0.41); scryfall='Sliver creatures you control have flying.'
- oracle_text advisory -- Striking Sliver: oracle_text diverges (similarity 0.31); scryfall='Sliver creatures you control have first strike. (They deal combat damage before creatures without first strike
- oracle_text advisory -- Cloudshredder Sliver: oracle_text diverges (similarity 0.48); scryfall='Sliver creatures you control have flying and haste.'
- oracle_text advisory -- Hibernation Sliver: oracle_text diverges (similarity 0.49); scryfall='All Slivers have "Pay 2 life: Return this permanent to its owner\'s hand."'
- oracle_text advisory -- Cavern of Souls: oracle_text diverges (similarity 0.40); scryfall="As this land enters, choose a creature type.
{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Unclaimed Territory: oracle_text diverges (similarity 0.15); scryfall='As this land enters, choose a creature type.
{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Secluded Courtyard: oracle_text diverges (similarity 0.21); scryfall='As this land enters, choose a creature type.
{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana o
- oracle_text advisory -- Mutavault: oracle_text diverges (similarity 0.56); scryfall="{T}: Add {C}.
{1}: This land becomes a 2/2 creature with all creature types until end of turn. It's still a l
- oracle_text advisory -- Aether Vial: oracle_text diverges (similarity 0.35); scryfall='At the beginning of your upkeep, you may put a charge counter on this artifact.
{T}: You may put a creature c
- oracle_text advisory -- Reliquary Tower: oracle_text diverges (similarity 0.44); scryfall='You have no maximum hand size.
{T}: Add {C}.'
- oracle_text advisory -- Dwarven Hold: oracle_text diverges (similarity 0.23); scryfall='This land enters tapped.
You may choose not to untap this land during your untap step.
At the beginning of y
- oracle_text advisory -- Mercadian Bazaar: oracle_text diverges (similarity 0.26); scryfall='This land enters tapped.
{T}: Put a storage counter on this land.
{T}, Remove any number of storage counters
- oracle_text advisory -- Temple of Epiphany: oracle_text diverges (similarity 0.60); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Thundering Falls: oracle_text diverges (similarity 0.63); scryfall='({T}: Add {U} or {R}.)
This land enters tapped.
When this land enters, surveil 1. (Look at the top card of y
- oracle_text advisory -- Land's Edge: oracle_text diverges (similarity 0.51); scryfall='Discard a card: If the discarded card was a land card, this enchantment deals 2 damage to target player or pla
- oracle_text advisory -- Throes of Chaos: oracle_text diverges (similarity 0.06); scryfall='Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland card tha
- oracle_text advisory -- Tournament Grounds: oracle_text diverges (similarity 0.37); scryfall='{T}: Add {C}.
{T}: Add {R}, {W}, or {B}. Spend this mana only to cast a Knight or Equipment spell.'
- oracle_text advisory -- Dauntless Bodyguard: oracle_text diverges (similarity 0.55); scryfall='As this creature enters, choose another creature you control.
Sacrifice this creature: The chosen creature ga
- oracle_text advisory -- Venerable Knight: oracle_text diverges (similarity 0.52); scryfall='When this creature dies, put a +1/+1 counter on target Knight you control.'
- oracle_text advisory -- Acclaimed Contender: oracle_text diverges (similarity 0.45); scryfall='When this creature enters, if you control another Knight, look at the top five cards of your library. You may 
- oracle_text advisory -- Knight Exemplar: oracle_text diverges (similarity 0.27); scryfall='First strike (This creature deals combat damage before creatures without first strike.)
Other Knight creature
- oracle_text advisory -- Valiant Knight: oracle_text diverges (similarity 0.18); scryfall='Other Knights you control get +1/+1.
{3}{W}{W}: Knights you control gain double strike until end of turn.'
- oracle_text advisory -- Kinsbaile Cavalier: oracle_text diverges (similarity 0.05); scryfall='Knight creatures you control have double strike.'
- oracle_text advisory -- Marshal of Zhalfir: oracle_text diverges (similarity 0.49); scryfall='Other Knights you control get +1/+1.
{W}{U}, {T}: Tap another target creature.'
- oracle_text advisory -- Haytham Kenway: oracle_text diverges (similarity 0.53); scryfall='Protection from Assassins
Other Knights you control get +2/+2 and have protection from Assassins.
When Hayth
- oracle_text advisory -- Adeline, Resplendent Cathar: oracle_text diverges (similarity 0.76); scryfall="Vigilance
Adeline's power is equal to the number of creatures you control.
Whenever you attack, for each opp
- oracle_text advisory -- Accorder Paladin: oracle_text diverges (similarity 0.33); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)'
- oracle_text advisory -- Silverblade Paladin: oracle_text diverges (similarity 0.25); scryfall='Soulbond (You may pair this creature with another unpaired creature when either enters. They remain paired for
- oracle_text advisory -- Hero of Bladehold: oracle_text diverges (similarity 0.50); scryfall='Battle cry (Whenever this creature attacks, each other attacking creature gets +1/+0 until end of turn.)
When
- oracle_text advisory -- Windswept Heath: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Plains card, put it onto the battlef
- oracle_text advisory -- Marsh Flats: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Plains or Swamp card, put it onto the battlefi
- oracle_text advisory -- Bloodstained Mire: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Mountain card, put it onto the battle
- oracle_text advisory -- Wooded Foothills: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Mountain or Forest card, put it onto the battl
- oracle_text advisory -- Grove of the Burnwillows: oracle_text diverges (similarity 0.20); scryfall='{T}: Add {C}.
{T}: Add {R} or {G}. Each opponent gains 1 life.'
- oracle_text advisory -- Ignoble Hierarch: oracle_text diverges (similarity 0.56); scryfall='Exalted (Whenever a creature you control attacks alone, that creature gets +1/+1 until end of turn.)
{T}: Add
- oracle_text advisory -- Skyshroud Cutter: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 5 life."
- oracle_text advisory -- Plague Drone: oracle_text diverges (similarity 0.70); scryfall='Flying
Rot Fly — If an opponent would gain life, that player loses that much life instead.'
- oracle_text advisory -- Aria of Flame: oracle_text diverges (similarity 0.78); scryfall='When this enchantment enters, each opponent gains 10 life.
Whenever you cast an instant or sorcery spell, put
- oracle_text advisory -- Fiery Justice: oracle_text diverges (similarity 0.54); scryfall='Fiery Justice deals 5 damage divided as you choose among any number of targets. Target opponent gains 5 life.'
- oracle_text advisory -- Swords to Plowshares: oracle_text diverges (similarity 0.17); scryfall='Exile target creature. Its controller gains life equal to its power.'
- oracle_text advisory -- Invigorate: oracle_text diverges (similarity 0.52); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have an opponent gain 3 life.
Target
- oracle_text advisory -- Reverent Silence: oracle_text diverges (similarity 0.38); scryfall="If you control a Forest, rather than pay this spell's mana cost, you may have each other player gain 6 life.

- oracle_text advisory -- Idyllic Tutor: oracle_text diverges (similarity 0.30); scryfall='Search your library for an enchantment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Enlightened Tutor: oracle_text diverges (similarity 0.32); scryfall='Search your library for an artifact or enchantment card, reveal it, then shuffle and put that card on top.'
- oracle_text advisory -- Forbidden Orchard: oracle_text diverges (similarity 0.22); scryfall='{T}: Add one mana of any color.
Whenever you tap this land for mana, target opponent creates a 1/1 colorless 
- oracle_text advisory -- Reflecting Pool: oracle_text diverges (similarity 0.26); scryfall='{T}: Add one mana of any type that a land you control could produce.'
- oracle_text advisory -- Izzet Signet: oracle_text diverges (similarity 0.11); scryfall='{1}, {T}: Add {U}{R}.'
- oracle_text advisory -- Ponder: oracle_text diverges (similarity 0.32); scryfall='Look at the top three cards of your library, then put them back in any order. You may shuffle.
Draw a card.'
- oracle_text advisory -- Preordain: oracle_text diverges (similarity 0.27); scryfall='Scry 2, then draw a card. (To scry 2, look at the top two cards of your library, then put any number of them o
- oracle_text advisory -- Expressive Iteration: oracle_text diverges (similarity 0.45); scryfall='Look at the top three cards of your library. Put one of them into your hand, put one of them on the bottom of 
- oracle_text advisory -- Crackle with Power: oracle_text diverges (similarity 0.17); scryfall='Crackle with Power deals five times X damage to each of up to X targets.'
- oracle_text advisory -- Remand: oracle_text diverges (similarity 0.58); scryfall="Counter target spell. If that spell is countered this way, put it into its owner's hand instead of into that p
- oracle_text advisory -- Memory Lapse: oracle_text diverges (similarity 0.69); scryfall="Counter target spell. If that spell is countered this way, put it on top of its owner's library instead of int
- oracle_text advisory -- Distorting Wake: oracle_text diverges (similarity 0.34); scryfall="Return X target nonland permanents to their owners' hands."
- oracle_text advisory -- Icy Blast: oracle_text diverges (similarity 0.64); scryfall="Tap X target creatures.
Ferocious — If you control a creature with power 4 or greater, those creatures don't 
- oracle_text advisory -- Hinata, Dawn-Crowned: oracle_text diverges (similarity 0.31); scryfall='Flying, trample
Spells you cast cost {1} less to cast for each target.
Spells your opponents cast cost {1} m
- oracle_text advisory -- Izzet Boilerworks: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {U}{
- oracle_text advisory -- Orzhov Basilica: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {W}{
- oracle_text advisory -- Soulfire Eruption: oracle_text diverges (similarity 0.28); scryfall="Choose any number of target creatures, planeswalkers, and/or players. For each of them, exile the top card of 
- oracle_text advisory -- Magma Opus: oracle_text diverges (similarity 0.46); scryfall='Magma Opus deals 4 damage divided as you choose among any number of targets. Tap two target permanents. Create
- oracle_text advisory -- Reality Spasm: oracle_text diverges (similarity 0.20); scryfall='Choose one —
• Tap X target permanents.
• Untap X target permanents.'
- oracle_text advisory -- Ornithopter of Paradise: oracle_text diverges (similarity 0.13); scryfall='Flying
{T}: Add one mana of any color.'
- oracle_text advisory -- Gamble: oracle_text diverges (similarity 0.22); scryfall='Search your library for a card, put that card into your hand, discard a card at random, then shuffle.'
- oracle_text advisory -- Irencrag Feat: oracle_text diverges (similarity 0.10); scryfall='Add seven {R}. You can cast only one more spell this turn.'
- oracle_text advisory -- Pyretic Ritual: oracle_text diverges (similarity 0.05); scryfall='Add {R}{R}{R}.'
- oracle_text advisory -- Seething Song: oracle_text diverges (similarity 0.08); scryfall='Add {R}{R}{R}{R}{R}.'
- oracle_text advisory -- Desperate Ritual: oracle_text diverges (similarity 0.18); scryfall="Add {R}{R}{R}.
Splice onto Arcane {1}{R} (As you cast an Arcane spell, you may reveal this card from your han
- oracle_text advisory -- Dragonlord Kolaghan: oracle_text diverges (similarity 0.53); scryfall='Flying, haste
Other creatures you control have haste.
Whenever an opponent casts a creature or planeswalker 
- oracle_text advisory -- Karrthus, Tyrant of Jund: oracle_text diverges (similarity 0.37); scryfall='Flying, haste
When Karrthus enters, gain control of all Dragons, then untap all Dragons.
Other Dragon creatu
- oracle_text advisory -- Ruby Medallion: oracle_text diverges (similarity 0.17); scryfall='Red spells you cast cost {1} less to cast.'
- oracle_text advisory -- Lotus Bloom: oracle_text diverges (similarity 0.25); scryfall='Suspend 3—{0} (Rather than cast this card from your hand, pay {0} and exile it with three time counters on it.
- oracle_text advisory -- Rite of Flame: oracle_text diverges (similarity 0.21); scryfall='Add {R}{R}, then add {R} for each card named Rite of Flame in each graveyard.'
- oracle_text advisory -- Scourge of Valkas: oracle_text diverges (similarity 0.32); scryfall='Flying
Whenever this creature or another Dragon you control enters, it deals X damage to any target, where X 
- oracle_text advisory -- Lathliss, Dragon Queen: oracle_text diverges (similarity 0.31); scryfall='Flying
Whenever another nontoken Dragon you control enters, create a 5/5 red Dragon creature token with flyin
- oracle_text advisory -- Utvara Hellkite: oracle_text diverges (similarity 0.24); scryfall='Flying
Whenever a Dragon you control attacks, create a 6/6 red Dragon creature token with flying.'
- oracle_text advisory -- Dragonstorm: oracle_text diverges (similarity 0.16); scryfall='Search your library for a Dragon permanent card, put it onto the battlefield, then shuffle.
Storm (When you c
- oracle_text advisory -- Apex of Power: oracle_text diverges (similarity 0.15); scryfall='Exile the top seven cards of your library. Until end of turn, you may cast spells from among them.
If this sp
- oracle_text advisory -- Slippery Bogle: oracle_text diverges (similarity 0.41); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Gladecover Scout: oracle_text diverges (similarity 0.76); scryfall="Hexproof (This creature can't be the target of spells or abilities your opponents control.)"
- oracle_text advisory -- Kor Spiritdancer: oracle_text diverges (similarity 0.53); scryfall='This creature gets +2/+2 for each Aura attached to it.
Whenever you cast an Aura spell, you may draw a card.'
- oracle_text advisory -- Light-Paws, Emperor's Voice: oracle_text diverges (similarity 0.74); scryfall='Whenever an Aura you control enters, if you cast it, you may search your library for an Aura card with mana va
- oracle_text advisory -- Ethereal Armor: oracle_text diverges (similarity 0.33); scryfall='Enchant creature
Enchanted creature gets +1/+1 for each enchantment you control and has first strike.'
- oracle_text advisory -- Rancor: oracle_text diverges (similarity 0.68); scryfall="Enchant creature
Enchanted creature gets +2/+0 and has trample.
When this Aura is put into a graveyard from 
- oracle_text advisory -- Daybreak Coronet: oracle_text diverges (similarity 0.35); scryfall='Enchant creature with another Aura attached to it
Enchanted creature gets +3/+3 and has first strike, vigilan
- oracle_text advisory -- Armadillo Cloak: oracle_text diverges (similarity 0.77); scryfall='Enchant creature
Enchanted creature gets +2/+2 and has trample.
Whenever enchanted creature deals damage, yo
- oracle_text advisory -- Hyena Umbra: oracle_text diverges (similarity 0.52); scryfall='Enchant creature
Enchanted creature gets +1/+1 and has first strike.
Umbra armor (If enchanted creature woul
- oracle_text advisory -- Spirit Mantle: oracle_text diverges (similarity 0.66); scryfall='Enchant creature
Enchanted creature gets +1/+1 and has protection from creatures.'
- oracle_text advisory -- Spider Umbra: oracle_text diverges (similarity 0.40); scryfall='Enchant creature
Enchanted creature gets +1/+1 and has reach. (It can block creatures with flying.)
Umbra ar
- oracle_text advisory -- Ancestral Mask: oracle_text diverges (similarity 0.59); scryfall='Enchant creature
Enchanted creature gets +2/+2 for each other enchantment on the battlefield.'
- oracle_text advisory -- Alpha Authority: oracle_text diverges (similarity 0.54); scryfall="Enchant creature
Enchanted creature has hexproof and can't be blocked by more than one creature."
- oracle_text advisory -- Gryff's Boon: oracle_text diverges (similarity 0.75); scryfall='Enchant creature
Enchanted creature gets +1/+0 and has flying.
{3}{W}: Return this card from your graveyard 
- oracle_text advisory -- Audacity: oracle_text diverges (similarity 0.59); scryfall="Enchant creature
Enchanted creature gets +2/+0 and has trample. (It can deal excess combat damage to the play
- oracle_text advisory -- All That Glitters: oracle_text diverges (similarity 0.57); scryfall='Enchant creature
Enchanted creature gets +1/+1 for each artifact and/or enchantment you control.'
- oracle_text advisory -- Spirit Link: oracle_text diverges (similarity 0.47); scryfall='Enchant creature (Target a creature as you cast this. This card enters attached to that creature.)
Whenever e
- oracle_text advisory -- Lion Umbra: oracle_text diverges (similarity 0.77); scryfall='Enchant modified creature (Equipment, Auras its controller controls, and counters are modifications.)
Enchant
- oracle_text advisory -- Brushland: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{T}: Add {G} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Branchloft Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Darkbore Pathway: oracle_text diverges (similarity 0.07); scryfall='{T}: Add {B}.'
- oracle_text advisory -- Goblin King: oracle_text diverges (similarity 0.31); scryfall='Other Goblins get +1/+1 and have mountainwalk.'
- oracle_text advisory -- Goblin Chieftain: oracle_text diverges (similarity 0.41); scryfall='Haste (This creature can attack and {T} as soon as it comes under your control.)
Other Goblin creatures you c
- oracle_text advisory -- Goblin Warchief: oracle_text diverges (similarity 0.52); scryfall='Goblin spells you cast cost {1} less to cast.
Goblins you control have haste.'
- oracle_text advisory -- Goblin Piledriver: oracle_text diverges (similarity 0.43); scryfall="Protection from blue (This creature can't be blocked, targeted, dealt damage, or enchanted by anything blue.)\
- oracle_text advisory -- Goblin Matron: oracle_text diverges (similarity 0.66); scryfall='When this creature enters, you may search your library for a Goblin card, reveal that card, put it into your h
- oracle_text advisory -- Mogg War Marshal: oracle_text diverges (similarity 0.56); scryfall='Echo {1}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Siege-Gang Commander: oracle_text diverges (similarity 0.58); scryfall='When this creature enters, create three 1/1 red Goblin creature tokens.
{1}{R}, Sacrifice a Goblin: This crea
- oracle_text advisory -- Skirk Prospector: oracle_text diverges (similarity 0.31); scryfall='Sacrifice a Goblin: Add {R}.'
- oracle_text advisory -- Krenko, Mob Boss: oracle_text diverges (similarity 0.43); scryfall='{T}: Create X 1/1 red Goblin creature tokens, where X is the number of Goblins you control.'
- oracle_text advisory -- Pashalik Mons: oracle_text diverges (similarity 0.52); scryfall='Whenever Pashalik Mons or another Goblin you control dies, Pashalik Mons deals 1 damage to any target.
{3}{R}
- oracle_text advisory -- Rundvelt Hordemaster: oracle_text diverges (similarity 0.36); scryfall="Other Goblins you control get +1/+1.
Whenever this creature or another Goblin you control dies, exile the top
- oracle_text advisory -- Goblin Lackey: oracle_text diverges (similarity 0.56); scryfall='Whenever this creature deals damage to a player, you may put a Goblin permanent card from your hand onto the b
- oracle_text advisory -- Muxus, Goblin Grandee: oracle_text diverges (similarity 0.46); scryfall='When Muxus enters, reveal the top six cards of your library. Put all Goblin creature cards with mana value 5 o
- oracle_text advisory -- Goblin Chainwhirler: oracle_text diverges (similarity 0.24); scryfall='First strike
When this creature enters, it deals 1 damage to each opponent and each creature and planeswalker
- oracle_text advisory -- Twinshot Sniper: oracle_text diverges (similarity 0.50); scryfall='Reach
When this creature enters, it deals 2 damage to any target.
Channel — {1}{R}, Discard this card: It de
- oracle_text advisory -- Stingscourger: oracle_text diverges (similarity 0.71); scryfall="Echo {3}{R} (At the beginning of your upkeep, if this came under your control since the beginning of your last
- oracle_text advisory -- Three Tree City: oracle_text diverges (similarity 0.47); scryfall='As Three Tree City enters, choose a creature type.
{T}: Add {C}.
{2}, {T}: Choose a color. Add an amount of 
- oracle_text advisory -- Hunted Phantasm: oracle_text diverges (similarity 0.34); scryfall="This creature can't be blocked.
When this creature enters, target opponent creates five 1/1 red Goblin creatu
- oracle_text advisory -- Suture Priest: oracle_text diverges (similarity 0.49); scryfall='Whenever another creature you control enters, you may gain 1 life.
Whenever a creature an opponent controls e
- oracle_text advisory -- Massacre Wurm: oracle_text diverges (similarity 0.38); scryfall='When this creature enters, creatures your opponents control get -2/-2 until end of turn.
Whenever a creature 
- oracle_text advisory -- Soul Warden: oracle_text diverges (similarity 0.15); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- Essence Warden: oracle_text diverges (similarity 0.24); scryfall='Whenever another creature enters, you gain 1 life.'
- oracle_text advisory -- City of Brass: oracle_text diverges (similarity 0.19); scryfall='Whenever this land becomes tapped, it deals 1 damage to you.
{T}: Add one mana of any color.'
- oracle_text advisory -- Defense of the Heart: oracle_text diverges (similarity 0.43); scryfall='At the beginning of your upkeep, if an opponent controls three or more creatures, sacrifice this enchantment, 
- oracle_text advisory -- Sylvan Scrying: oracle_text diverges (similarity 0.47); scryfall='Search your library for a land card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Crop Rotation: oracle_text diverges (similarity 0.42); scryfall='As an additional cost to cast this spell, sacrifice a land.
Search your library for a land card, put that car
- oracle_text advisory -- Varchild's War-Riders: oracle_text diverges (similarity 0.58); scryfall='Cumulative upkeep—Have an opponent create a 1/1 red Survivor creature token. (At the beginning of your upkeep,
- oracle_text advisory -- Azorius Chancery: oracle_text diverges (similarity 0.38); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {W}{
- oracle_text advisory -- Tree of Tales: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}.'
- oracle_text advisory -- Misty Rainforest: oracle_text diverges (similarity 0.36); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Forest or Island card, put it onto the battlef
- oracle_text advisory -- Verdant Catacombs: oracle_text diverges (similarity 0.28); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for a Swamp or Forest card, put it onto the battlefi
- oracle_text advisory -- Scalding Tarn: oracle_text diverges (similarity 0.29); scryfall='{T}, Pay 1 life, Sacrifice this land: Search your library for an Island or Mountain card, put it onto the batt
- oracle_text advisory -- Cosmic Spider-Man: oracle_text diverges (similarity 0.33); scryfall='Flying, first strike, trample, lifelink, haste
At the beginning of combat on your turn, other Spiders you con
- oracle_text advisory -- Mana Cannons: oracle_text diverges (similarity 0.44); scryfall='Whenever you cast a multicolored spell, this enchantment deals X damage to any target, where X is the number o
- oracle_text advisory -- Ancient Cornucopia: oracle_text diverges (similarity 0.43); scryfall="Whenever you cast a spell that's one or more colors, you may gain 1 life for each of that spell's colors. Do t
- oracle_text advisory -- Two-Headed Hellkite: oracle_text diverges (similarity 0.26); scryfall='Flying, menace, haste
Whenever this creature attacks, draw two cards.'
- oracle_text advisory -- Progenitus: oracle_text diverges (similarity 0.28); scryfall="Protection from everything
If Progenitus would be put into a graveyard from anywhere, reveal Progenitus and s
- oracle_text advisory -- Faeburrow Elder: oracle_text diverges (similarity 0.36); scryfall='Vigilance
This creature gets +1/+1 for each color among permanents you control.
{T}: For each color among pe
- oracle_text advisory -- Bloom Tender: oracle_text diverges (similarity 0.52); scryfall='Vivid — {T}: For each color among permanents you control, add one mana of that color.'
- oracle_text advisory -- Deathrite Shaman: oracle_text diverges (similarity 0.46); scryfall='{T}: Exile target land card from a graveyard. Add one mana of any color. (Activate only as an instant.)
{B}, 
- oracle_text advisory -- Lightning Greaves: oracle_text diverges (similarity 0.18); scryfall="Equipped creature has haste and shroud. (It can't be the target of spells or abilities.)
Equip {0}"
- oracle_text advisory -- Maelstrom Archangel: oracle_text diverges (similarity 0.31); scryfall='Flying
Whenever this creature deals combat damage to a player, you may cast a spell from your hand without pa
- oracle_text advisory -- Jared Carthalion: oracle_text diverges (similarity 0.60); scryfall="+1: Create a 3/3 Kavu creature token with trample that's all colors.
−3: Choose up to two target creatures. F
- oracle_text advisory -- Nicol Bolas, Planeswalker: oracle_text diverges (similarity 0.21); scryfall="+3: Destroy target noncreature permanent.
−2: Gain control of target creature.
−9: Nicol Bolas deals 7 damag
- oracle_text advisory -- Oko, Thief of Crowns: oracle_text diverges (similarity 0.42); scryfall='+2: Create a Food token. (It\'s an artifact with "{2}, {T}, Sacrifice this token: You gain 3 life.")
+1: Targ
- oracle_text advisory -- Garth One-Eye: oracle_text diverges (similarity 0.39); scryfall="{T}: Choose a card name that hasn't been chosen from among Disenchant, Braingeyser, Terror, Shivan Dragon, Reg
- oracle_text advisory -- Black Lotus: oracle_text diverges (similarity 0.36); scryfall='{T}, Sacrifice this artifact: Add three mana of any one color.'
- oracle_text advisory -- Braingeyser: oracle_text diverges (similarity 0.23); scryfall='Target player draws X cards.'
- oracle_text advisory -- Terror: oracle_text diverges (similarity 0.32); scryfall="Destroy target nonartifact, nonblack creature. It can't be regenerated."
- oracle_text advisory -- Shivan Dragon: oracle_text diverges (similarity 0.32); scryfall='Flying
{R}: This creature gets +1/+0 until end of turn.'
- oracle_text advisory -- Regrowth: oracle_text diverges (similarity 0.46); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Unite the Coalition: oracle_text diverges (similarity 0.46); scryfall="Choose five. You may choose the same mode more than once.
• Target permanent phases out.
• Target player dra
- oracle_text advisory -- Disenchant: oracle_text diverges (similarity 0.21); scryfall='Destroy target artifact or enchantment.'
- oracle_text advisory -- Mirrorwing Dragon: oracle_text diverges (similarity 0.42); scryfall='Flying
Whenever a player casts an instant or sorcery spell that targets only this creature, that player copie
- oracle_text advisory -- Zada, Hedron Grinder: oracle_text diverges (similarity 0.57); scryfall='Whenever you cast an instant or sorcery spell that targets only Zada, copy that spell for each other creature 
- oracle_text advisory -- Goblin Instigator: oracle_text diverges (similarity 0.32); scryfall='When this creature enters, create a 1/1 red Goblin creature token.'
- oracle_text advisory -- Fists of Flame: oracle_text diverges (similarity 0.36); scryfall="Draw a card. Until end of turn, target creature gains trample and gets +1/+0 for each card you've drawn this t
- oracle_text advisory -- Luxurious Libation: oracle_text diverges (similarity 0.24); scryfall='Target creature gets +X/+X until end of turn. Create a 1/1 green and white Citizen creature token.'
- oracle_text advisory -- Fortifying Draught: oracle_text diverges (similarity 0.33); scryfall='You gain 2 life. Target creature gets +X/+X until end of turn, where X is the amount of life you gained this t
- oracle_text advisory -- Gold Rush: oracle_text diverges (similarity 0.36); scryfall='Create a Treasure token. Until end of turn, up to one target creature gets +2/+2 for each Treasure you control
- oracle_text advisory -- Ancestral Anger: oracle_text diverges (similarity 0.52); scryfall='Target creature gains trample and gets +X/+0 until end of turn, where X is 1 plus the number of cards named An
- oracle_text advisory -- Oracle's Restoration: oracle_text diverges (similarity 0.10); scryfall='Target creature you control gets +1/+1 until end of turn. You draw a card and gain 1 life.'
- oracle_text advisory -- Expedite: oracle_text diverges (similarity 0.29); scryfall='Target creature gains haste until end of turn.
Draw a card.'
- oracle_text advisory -- Impolite Entrance: oracle_text diverges (similarity 0.18); scryfall='Target creature gains trample and haste until end of turn.
Draw a card.'
- oracle_text advisory -- Scale the Heights: oracle_text diverges (similarity 0.49); scryfall='Put a +1/+1 counter on up to one target creature. You gain 2 life. You may play an additional land this turn.\
- oracle_text advisory -- Twinflame: oracle_text diverges (similarity 0.42); scryfall="Strive — This spell costs {2}{R} more to cast for each target beyond the first.
Choose any number of target c
- oracle_text advisory -- Gruul Turf: oracle_text diverges (similarity 0.39); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {R}{
- oracle_text advisory -- Kazandu Refuge: oracle_text diverges (similarity 0.50); scryfall='This land enters tapped.
When this land enters, you gain 1 life.
{T}: Add {R} or {G}.'
- oracle_text advisory -- Rootbound Crag: oracle_text diverges (similarity 0.43); scryfall='This land enters tapped unless you control a Mountain or a Forest.
{T}: Add {R} or {G}.'
- oracle_text advisory -- Colossus Hammer: oracle_text diverges (similarity 0.25); scryfall='Equipped creature gets +10/+10 and loses flying.
Equip {8} ({8}: Attach to target creature you control. Equip
- oracle_text advisory -- Loxodon Warhammer: oracle_text diverges (similarity 0.36); scryfall='Equipped creature gets +3/+0 and has trample and lifelink.
Equip {3}'
- oracle_text advisory -- Shadowspear: oracle_text diverges (similarity 0.54); scryfall='Equipped creature gets +1/+1 and has trample and lifelink.
{1}: Permanents your opponents control lose hexpro
- oracle_text advisory -- Grafted Wargear: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +3/+2.
Whenever this Equipment becomes unattached from a permanent, sacrifice that per
- oracle_text advisory -- O-Naginata: oracle_text diverges (similarity 0.49); scryfall='This Equipment can be attached only to a creature with power 3 or greater.
Equipped creature gets +3/+0 and h
- oracle_text advisory -- Ancient Den: oracle_text diverges (similarity 0.02); scryfall='{T}: Add {W}.'
- oracle_text advisory -- Bone Saw: oracle_text diverges (similarity 0.29); scryfall='Equipped creature gets +1/+0.
Equip {1} ({1}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Cathar's Shield: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +0/+3 and has vigilance.
Equip {3} ({3}: Attach to target creature you control. Equip 
- oracle_text advisory -- Accorder's Shield: oracle_text diverges (similarity 0.23); scryfall="Equipped creature gets +0/+3 and has vigilance. (Attacking doesn't cause it to tap.)
Equip {3} ({3}: Attach t
- oracle_text advisory -- Kite Shield: oracle_text diverges (similarity 0.52); scryfall='Equipped creature gets +0/+3.
Equip {3} ({3}: Attach to target creature you control. Equip only as a sorcery.
- oracle_text advisory -- Spidersilk Net: oracle_text diverges (similarity 0.22); scryfall='Equipped creature gets +0/+2 and has reach. (It can block creatures with flying.)
Equip {2} ({2}: Attach to t
- oracle_text advisory -- Golem-Skin Gauntlets: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+0 for each Equipment attached to it.
Equip {2} ({2}: Attach to target creature you
- oracle_text advisory -- Skateboard: oracle_text diverges (similarity 0.19); scryfall='When this Equipment enters, tap target permanent.
Equipped creature gets +1/+0 and has haste.
Equip {1} ({1}
- oracle_text advisory -- Dragonfire Blade: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +2/+2 and has hexproof from monocolored.
Equip {4}. This ability costs {1} less to act
- oracle_text advisory -- Deconstruction Hammer: oracle_text diverges (similarity 0.17); scryfall='Equipped creature gets +1/+1 and has "{3}, {T}, Sacrifice Deconstruction Hammer: Destroy target artifact or en
- oracle_text advisory -- Sram, Senior Edificer: oracle_text diverges (similarity 0.08); scryfall='Whenever you cast an Aura, Equipment, or Vehicle spell, draw a card.'
- oracle_text advisory -- Cid, Freeflier Pilot: oracle_text diverges (similarity 0.13); scryfall='Equipment and Vehicle spells you cast cost {1} less to cast.
Jump — During your turn, Cid has flying.
{2}, {
- oracle_text advisory -- Sigarda's Aid: oracle_text diverges (similarity 0.14); scryfall='You may cast Aura and Equipment spells as though they had flash.
Whenever an Equipment you control enters, yo
- oracle_text advisory -- Dwalin, Weaponmaster: oracle_text diverges (similarity 0.12); scryfall='First strike
Whenever Dwalin enters or attacks, put a hone counter on each Equipment you control. (Each hone 
- oracle_text advisory -- Ardenn, Intrepid Archaeologist: oracle_text diverges (similarity 0.17); scryfall='At the beginning of combat on your turn, you may attach any number of Auras and Equipment you control to targe
- oracle_text advisory -- Umezawa's Jitte: oracle_text diverges (similarity 0.47); scryfall="Whenever equipped creature deals combat damage, put two charge counters on Umezawa's Jitte.
Remove a charge c
- oracle_text advisory -- Kor Duelist: oracle_text diverges (similarity 0.43); scryfall='As long as this creature is equipped, it has double strike. (It deals both first-strike and regular combat dam
- oracle_text advisory -- Puresteel Paladin: oracle_text diverges (similarity 0.34); scryfall='Whenever an Equipment you control enters, you may draw a card.
Metalcraft — Equipment you control have equip 
- oracle_text advisory -- Balan, Wandering Knight: oracle_text diverges (similarity 0.30); scryfall='First strike
Balan has double strike as long as two or more Equipment are attached to it.
{1}{W}: Attach all
- oracle_text advisory -- Armored Skyhunter: oracle_text diverges (similarity 0.49); scryfall='Flying
Whenever this creature attacks, look at the top six cards of your library. You may put an Aura or Equi
- oracle_text advisory -- Kemba, Kha Regent: oracle_text diverges (similarity 0.34); scryfall='At the beginning of your upkeep, create a 2/2 white Cat creature token for each Equipment attached to Kemba.'
- oracle_text advisory -- Stoneforge Mystic: oracle_text diverges (similarity 0.44); scryfall='When this creature enters, you may search your library for an Equipment card, reveal it, put it into your hand
- oracle_text advisory -- Unexpectedly Absent: oracle_text diverges (similarity 0.28); scryfall="Put target nonland permanent into its owner's library just beneath the top X cards of that library."
- oracle_text advisory -- Boros Garrison: oracle_text diverges (similarity 0.34); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {R}{
- oracle_text advisory -- Elvish Archdruid: oracle_text diverges (similarity 0.38); scryfall='Other Elf creatures you control get +1/+1.
{T}: Add {G} for each Elf you control.'
- oracle_text advisory -- Priest of Titania: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {G} for each Elf on the battlefield.'
- oracle_text advisory -- Arbor Elf: oracle_text diverges (similarity 0.12); scryfall='{T}: Untap target Forest.'
- oracle_text advisory -- Wirewood Lodge: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.
{G}, {T}: Untap target Elf.'
- oracle_text advisory -- Worldly Tutor: oracle_text diverges (similarity 0.39); scryfall='Search your library for a creature card, reveal it, then shuffle and put the card on top.'
- oracle_text advisory -- Mirri's Guile: oracle_text diverges (similarity 0.44); scryfall='At the beginning of your upkeep, you may look at the top three cards of your library, then put them back in an
- oracle_text advisory -- Call of the Wild: oracle_text diverges (similarity 0.52); scryfall="{2}{G}{G}: Reveal the top card of your library. If it's a creature card, put it onto the battlefield. Otherwis
- oracle_text advisory -- Hornet Queen: oracle_text diverges (similarity 0.41); scryfall='Flying, deathtouch
When this creature enters, create four 1/1 green Insect creature tokens with flying and de
- oracle_text advisory -- Terastodon: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, you may destroy up to three target noncreature permanents. For each permanent put i
- oracle_text advisory -- Elderscale Wurm: oracle_text diverges (similarity 0.53); scryfall='Trample
When this creature enters, if your life total is less than 7, your life total becomes 7.
As long as 
- oracle_text advisory -- Craterhoof Behemoth: oracle_text diverges (similarity 0.44); scryfall='Haste
When this creature enters, creatures you control gain trample and get +X/+X until end of turn, where X 
- oracle_text advisory -- Worldspine Wurm: oracle_text diverges (similarity 0.39); scryfall="Trample
When this creature dies, create three 5/5 green Wurm creature tokens with trample.
When Worldspine W
- oracle_text advisory -- Vaultborn Tyrant: oracle_text diverges (similarity 0.47); scryfall="Trample
Whenever this creature or another creature you control with power 4 or greater enters, you gain 3 lif
- oracle_text advisory -- Natural Order: oracle_text diverges (similarity 0.38); scryfall='As an additional cost to cast this spell, sacrifice a green creature.
Search your library for a green creatur
- oracle_text advisory -- Turntimber Symbiosis: oracle_text diverges (similarity 0.45); scryfall='Look at the top seven cards of your library. You may put a creature card from among them onto the battlefield.
- oracle_text advisory -- Boros Reckoner: oracle_text diverges (similarity 0.17); scryfall='Whenever this creature is dealt damage, it deals that much damage to any target.
{R/W}: This creature gains f
- oracle_text advisory -- Burning-Fist Minotaur: oracle_text diverges (similarity 0.12); scryfall='First strike
{1}{R}, Discard a card: This creature gets +2/+0 until end of turn.'
- oracle_text advisory -- Deathbellow Raider: oracle_text diverges (similarity 0.16); scryfall='This creature attacks each combat if able.
{2}{B}: Regenerate this creature.'
- oracle_text advisory -- Fanatic of Mogis: oracle_text diverges (similarity 0.39); scryfall='When this creature enters, it deals damage to each opponent equal to your devotion to red. (Each {R} in the ma
- oracle_text advisory -- Gnarled Scarhide: oracle_text diverges (similarity 0.34); scryfall="Bestow {3}{B} (If you cast this card for its bestow cost, it's an Aura spell with enchant creature. It becomes
- oracle_text advisory -- Kragma Warcaller: oracle_text diverges (similarity 0.33); scryfall='Minotaur creatures you control have haste.
Whenever a Minotaur you control attacks, it gets +2/+0 until end o
- oracle_text advisory -- Neheb, the Worthy: oracle_text diverges (similarity 0.25); scryfall='First strike
Other Minotaurs you control have first strike.
As long as you have one or fewer cards in hand, 
- oracle_text advisory -- Rageblood Shaman: oracle_text diverges (similarity 0.35); scryfall='Trample
Other Minotaur creatures you control get +1/+1 and have trample.'
- oracle_text advisory -- Ragemonger: oracle_text diverges (similarity 0.45); scryfall='Minotaur spells you cast cost {B}{R} less to cast. This effect reduces only the amount of colored mana you pay
- oracle_text advisory -- Rakdos Carnarium: oracle_text diverges (similarity 0.36); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {B}{
- oracle_text advisory -- Sethron, Hurloon General: oracle_text diverges (similarity 0.34); scryfall='Whenever Sethron or another nontoken Minotaur you control enters, create a 2/3 red Minotaur creature token.
{
- oracle_text advisory -- Slaughter-Priest of Mogis: oracle_text diverges (similarity 0.21); scryfall='Whenever you sacrifice a permanent, this creature gets +2/+0 until end of turn.
{2}, Sacrifice another creatu
- oracle_text advisory -- Atsushi, the Blazing Sky: oracle_text diverges (similarity 0.43); scryfall='Flying, trample
When Atsushi dies, choose one —
• Exile the top two cards of your library. Until the end of 
- oracle_text advisory -- Inferno of the Star Mounts: oracle_text diverges (similarity 0.35); scryfall="This spell can't be countered.
Flying, haste
{R}: Inferno of the Star Mounts gets +1/+0 until end of turn. W
- oracle_text advisory -- Dragon Tempest: oracle_text diverges (similarity 0.34); scryfall='Whenever a creature you control with flying enters, it gains haste until end of turn.
Whenever a Dragon you c
- oracle_text advisory -- Urza's Incubator: oracle_text diverges (similarity 0.15); scryfall='As this artifact enters, choose a creature type.
Creature spells of the chosen type cost {2} less to cast.'
- oracle_text advisory -- Mind Stone: oracle_text diverges (similarity 0.25); scryfall='{T}: Add {C}.
{1}, {T}, Sacrifice this artifact: Draw a card.'
- oracle_text advisory -- Fire Diamond: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.
{T}: Add {R}.'
- oracle_text advisory -- Dragonspeaker Shaman: oracle_text diverges (similarity 0.15); scryfall='Dragon spells you cast cost {2} less to cast.'
- oracle_text advisory -- Glorybringer: oracle_text diverges (similarity 0.46); scryfall="Flying, haste
You may exert this creature as it attacks. When you do, it deals 4 damage to target non-Dragon 
- oracle_text advisory -- Haven of the Spirit Dragon: oracle_text diverges (similarity 0.32); scryfall='{T}: Add {C}.
{T}: Add one mana of any color. Spend this mana only to cast a Dragon creature spell.
{2}, {T}
- oracle_text advisory -- Nest Invader: oracle_text diverges (similarity 0.27); scryfall='When this creature enters, create a 0/1 colorless Eldrazi Spawn creature token. It has "Sacrifice this token: 
- oracle_text advisory -- Young Pyromancer: oracle_text diverges (similarity 0.28); scryfall='Whenever you cast an instant or sorcery spell, create a 1/1 red Elemental creature token.'
- oracle_text advisory -- Undercellar Myconid: oracle_text diverges (similarity 0.39); scryfall='Whenever this creature enters or dies, create a 1/1 green Saproling creature token.
{T}: Add one mana of any 
- oracle_text advisory -- Frontline Heroism: oracle_text diverges (similarity 0.39); scryfall='When this enchantment enters, create a 1/1 red Soldier creature token with haste.
Whenever you cast a spell t
- oracle_text advisory -- Adarkar Wastes: oracle_text diverges (similarity 0.29); scryfall='{T}: Add {C}.
{T}: Add {W} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Caves of Koilos: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.
{T}: Add {W} or {B}. This land deals 1 damage to you.'
- oracle_text advisory -- Yavimaya Coast: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.
{T}: Add {G} or {U}. This land deals 1 damage to you.'
- oracle_text advisory -- Llanowar Wastes: oracle_text diverges (similarity 0.68); scryfall='{T}: Add {C}.
{T}: Add {B} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Conservatory: oracle_text diverges (similarity 0.64); scryfall='This land enters tapped.
{T}: Add {G} or {W}.
{4}, {T}: Investigate. (Create a Clue token. It\'s an artifact
- oracle_text advisory -- Shivan Gorge: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{2}{R}, {T}: Shivan Gorge deals 1 damage to each opponent.'
- oracle_text advisory -- Mariposa Military Base: oracle_text diverges (similarity 0.26); scryfall='You may have this land enter tapped. If you do, you get two rad counters.
{T}: Add {C}.
{5}, {T}: Draw a car
- oracle_text advisory -- Eldrazi Displacer: oracle_text diverges (similarity 0.47); scryfall="Devoid (This card has no color.)
{2}{C}: Exile another target creature, then return it to the battlefield tap
- oracle_text advisory -- Emiel the Blessed: oracle_text diverges (similarity 0.48); scryfall="{3}: Exile another target creature you control, then return it to the battlefield under its owner's control.

- oracle_text advisory -- Cloud of Faeries: oracle_text diverges (similarity 0.25); scryfall='Flying
When this creature enters, untap up to two lands.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Peregrine Drake: oracle_text diverges (similarity 0.22); scryfall='Flying
When this creature enters, untap up to five lands.'
- oracle_text advisory -- Wild Growth: oracle_text diverges (similarity 0.50); scryfall='Enchant land
Whenever enchanted land is tapped for mana, its controller adds an additional {G}.'
- oracle_text advisory -- Cryptolith Rite: oracle_text diverges (similarity 0.05); scryfall='Creatures you control have "{T}: Add one mana of any color."'
- oracle_text advisory -- Overgrowth: oracle_text diverges (similarity 0.61); scryfall='Enchant land
Whenever enchanted land is tapped for mana, its controller adds an additional {G}{G}.'
- oracle_text advisory -- Fertile Ground: oracle_text diverges (similarity 0.49); scryfall='Enchant land
Whenever enchanted land is tapped for mana, its controller adds an additional one mana of any co
- oracle_text advisory -- Trace of Abundance: oracle_text diverges (similarity 0.34); scryfall="Enchant land
Enchanted land has shroud. (It can't be the target of spells or abilities.)
Whenever enchanted 
- oracle_text advisory -- Training Grounds: oracle_text diverges (similarity 0.49); scryfall="Activated abilities of creatures you control cost {2} less to activate. This effect can't reduce the mana in t
- oracle_text advisory -- Eladamri's Call: oracle_text diverges (similarity 0.61); scryfall='Search your library for a creature card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Stroke of Genius: oracle_text diverges (similarity 0.22); scryfall='Target player draws X cards.'
- oracle_text advisory -- Vexing Shusher: oracle_text diverges (similarity 0.05); scryfall="This spell can't be countered.
{R/G}: Target spell can't be countered."
- oracle_text advisory -- Essence Depleter: oracle_text diverges (similarity 0.13); scryfall='Devoid (This card has no color.)
{1}{C}: Target opponent loses 1 life and you gain 1 life. ({C} represents co
- oracle_text advisory -- Dimensional Infiltrator: oracle_text diverges (similarity 0.14); scryfall="Devoid (This card has no color.)
Flash
Flying
{1}{C}: Target opponent exiles the top card of their library.
- oracle_text advisory -- Living Wish: oracle_text diverges (similarity 0.08); scryfall='You may reveal a creature or land card you own from outside the game and put it into your hand. Exile Living W
- oracle_text advisory -- Aether Hub: oracle_text diverges (similarity 0.08); scryfall='When this land enters, you get {E} (an energy counter).
{T}: Add {C}.
{T}, Pay {E}: Add one mana of any colo
- oracle_text advisory -- Maelstrom Wanderer: oracle_text diverges (similarity 0.34); scryfall='Creatures you control have haste.
Cascade, cascade (When you cast this spell, exile cards from the top of you
- oracle_text advisory -- Annoyed Altisaur: oracle_text diverges (similarity 0.50); scryfall='Reach, trample
Cascade (When you cast this spell, exile cards from the top of your library until you exile a 
- oracle_text advisory -- Sakashima's Protege: oracle_text diverges (similarity 0.28); scryfall='Flash
Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Boarding Party: oracle_text diverges (similarity 0.62); scryfall='Haste
Cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland c
- oracle_text advisory -- Breaching Dragonstorm: oracle_text diverges (similarity 0.26); scryfall="When this enchantment enters, exile cards from the top of your library until you exile a nonland card. You may
- oracle_text advisory -- Call Forth the Tempest: oracle_text diverges (similarity 0.40); scryfall="Cascade, cascade (When you cast this spell, exile cards from the top of your library until you exile a nonland
- oracle_text advisory -- Creative Technique: oracle_text diverges (similarity 0.33); scryfall='Demonstrate (When you cast this spell, you may copy it. If you do, choose an opponent to also copy it.)
Shuff
- oracle_text advisory -- Dwarven Ruins: oracle_text diverges (similarity 0.09); scryfall='This land enters tapped.
{T}: Add {R}.
{T}, Sacrifice this land: Add {R}{R}.'
- oracle_text advisory -- Svyelunite Temple: oracle_text diverges (similarity 0.22); scryfall='This land enters tapped.
{T}: Add {U}.
{T}, Sacrifice this land: Add {U}{U}.'
- oracle_text advisory -- Hickory Woodlot: oracle_text diverges (similarity 0.24); scryfall='This land enters tapped with two depletion counters on it.
{T}, Remove a depletion counter from this land: Ad
- oracle_text advisory -- Melira, Sylvok Outcast: oracle_text diverges (similarity 0.34); scryfall="You can't get poison counters.
Creatures you control can't have -1/-1 counters put on them.
Creatures your o
- oracle_text advisory -- Vizier of Remedies: oracle_text diverges (similarity 0.55); scryfall='If one or more -1/-1 counters would be put on a creature you control, that many -1/-1 counters minus one are p
- oracle_text advisory -- Kitchen Finks: oracle_text diverges (similarity 0.44); scryfall="When this creature enters, you gain 2 life.
Persist (When this creature dies, if it had no -1/-1 counters on 
- oracle_text advisory -- Murderous Redcap: oracle_text diverges (similarity 0.51); scryfall="When this creature enters, it deals damage equal to its power to any target.
Persist (When this creature dies
- oracle_text advisory -- Carrion Feeder: oracle_text diverges (similarity 0.28); scryfall="This creature can't block.
Sacrifice a creature: Put a +1/+1 counter on this creature."
- oracle_text advisory -- Bloodthrone Vampire: oracle_text diverges (similarity 0.26); scryfall='Sacrifice a creature: This creature gets +2/+2 until end of turn.'
- oracle_text advisory -- Recruiter of the Guard: oracle_text diverges (similarity 0.35); scryfall='When this creature enters, you may search your library for a creature card with toughness 2 or less, reveal it
- oracle_text advisory -- Ranger of Eos: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, you may search your library for up to two creature cards with mana value 1 or less,
- oracle_text advisory -- Severance Priest: oracle_text diverges (similarity 0.40); scryfall="Deathtouch
When this creature enters, target opponent reveals their hand. You may choose a nonland card from 
- oracle_text advisory -- Birthing Pod: oracle_text diverges (similarity 0.25); scryfall="({G/P} can be paid with either {G} or 2 life.)
{1}{G/P}, {T}, Sacrifice a creature: Search your library for a
- oracle_text advisory -- Chord of Calling: oracle_text diverges (similarity 0.25); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1} 
- oracle_text advisory -- Reveillark: oracle_text diverges (similarity 0.22); scryfall="Flying
When this creature leaves the battlefield, return up to two target creature cards with power 2 or less
- oracle_text advisory -- Felidar Guardian: oracle_text diverges (similarity 0.18); scryfall="When this creature enters, you may exile another target permanent you control, then return that card to the ba
- oracle_text advisory -- Voice of Resurgence: oracle_text diverges (similarity 0.32); scryfall='Whenever an opponent casts a spell during your turn and when this creature dies, create a green and white Elem
- oracle_text advisory -- Scavenging Ooze: oracle_text diverges (similarity 0.23); scryfall='{G}: Exile target card from a graveyard. If it was a creature card, put a +1/+1 counter on this creature and y
- oracle_text advisory -- Ravenous Chupacabra: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, destroy target creature an opponent controls.'
- oracle_text advisory -- Reclamation Sage: oracle_text diverges (similarity 0.16); scryfall='When this creature enters, you may destroy target artifact or enchantment.'
- oracle_text advisory -- Celes, Rune Knight: oracle_text diverges (similarity 0.25); scryfall='When Celes enters, discard any number of cards, then draw that many cards plus one.
Whenever one or more othe
- oracle_text advisory -- Drifting Meadow: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.
{T}: Add {W}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Polluted Mire: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.
{T}: Add {B}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Smoldering Crater: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped.
{T}: Add {R}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Blasted Landscape: oracle_text diverges (similarity 0.30); scryfall='{T}: Add {C}.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fetid Pools: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {U} or {B}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Irrigated Farmland: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {W} or {U}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Canyon Slough: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {R}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Sheltered Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {G}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Scattered Groves: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {G} or {W}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Glittering Massif: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {R} or {W}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Festering Thicket: oracle_text diverges (similarity 0.39); scryfall='({T}: Add {B} or {G}.)
This land enters tapped.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Capital City: oracle_text diverges (similarity 0.11); scryfall='{T}: Add {C}.
{1}, {T}: Add one mana of any color.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Forsake the Worldly: oracle_text diverges (similarity 0.22); scryfall='Exile target artifact or enchantment.
Cycling {2} ({2}, Discard this card: Draw a card.)'
- oracle_text advisory -- Fluctuator: oracle_text diverges (similarity 0.17); scryfall='Cycling abilities you activate cost {2} less to activate.'
- oracle_text advisory -- Drannith Stinger: oracle_text diverges (similarity 0.26); scryfall='Whenever you cycle another card, this creature deals 1 damage to each opponent.
Cycling {1} ({1}, Discard thi
- oracle_text advisory -- Hollow One: oracle_text diverges (similarity 0.20); scryfall="This spell costs {2} less to cast for each card you've cycled or discarded this turn.
Cycling {2} ({2}, Disca
- oracle_text advisory -- Unearth: oracle_text diverges (similarity 0.24); scryfall='Return target creature card with mana value 3 or less from your graveyard to the battlefield.
Cycling {2} ({2
- oracle_text advisory -- Scrying Sheets: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{1}{S}, {T}: Look at the top card of your library. If that card is snow, you may reveal it and 
- oracle_text advisory -- Skred: oracle_text diverges (similarity 0.23); scryfall='Skred deals damage to target creature equal to the number of snow permanents you control.'
- oracle_text advisory -- Coldsteel Heart: oracle_text diverges (similarity 0.11); scryfall='This artifact enters tapped.
As this artifact enters, choose a color.
{T}: Add one mana of the chosen color.
- oracle_text advisory -- Abominable Treefolk: oracle_text diverges (similarity 0.35); scryfall="Trample
Abominable Treefolk's power and toughness are each equal to the number of snow permanents you control
- oracle_text advisory -- Arcum's Astrolabe: oracle_text diverges (similarity 0.25); scryfall='({S} can be paid with one mana from a snow source.)
When this artifact enters, draw a card.
{1}, {T}: Add on
- oracle_text advisory -- Frost Augur: oracle_text diverges (similarity 0.39); scryfall="{S}, {T}: Look at the top card of your library. If it's a snow card, you may reveal it and put it into your ha
- oracle_text advisory -- Ice-Fang Coatl: oracle_text diverges (similarity 0.21); scryfall='Flash
Flying
When this creature enters, draw a card.
This creature has deathtouch as long as you control at
- oracle_text advisory -- Jorn, God of Winter: oracle_text diverges (similarity 0.10); scryfall='Whenever Jorn attacks, untap each snow permanent you control.'
- oracle_text advisory -- Kaldring, the Rimestaff: oracle_text diverges (similarity 0.17); scryfall='{T}: You may play target snow permanent card from your graveyard this turn. If you do, it enters tapped.'
- oracle_text advisory -- Marit Lage's Slumber: oracle_text diverges (similarity 0.35); scryfall="Whenever Marit Lage's Slumber or another snow permanent you control enters, scry 1.
At the beginning of your 
- oracle_text advisory -- Rimefeather Owl: oracle_text diverges (similarity 0.26); scryfall="Flying
Rimefeather Owl's power and toughness are each equal to the number of snow permanents on the battlefie
- oracle_text advisory -- Rimescale Dragon: oracle_text diverges (similarity 0.29); scryfall="Flying
{2}{S}: Tap target creature and put an ice counter on it. ({S} can be paid with one mana from a snow s
- oracle_text advisory -- Soul's Attendant: oracle_text diverges (similarity 0.12); scryfall='Whenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Auriok Champion: oracle_text diverges (similarity 0.11); scryfall='Protection from black and from red
Whenever another creature enters, you may gain 1 life.'
- oracle_text advisory -- Ocelot Pride: oracle_text diverges (similarity 0.12); scryfall="First strike, lifelink
Ascend (If you control ten or more permanents, you get the city's blessing for the res
- oracle_text advisory -- Serra Ascendant: oracle_text diverges (similarity 0.21); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)
As long as you have 30 or mo
- oracle_text advisory -- Ajani's Pridemate: oracle_text diverges (similarity 0.09); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Voice of the Blessed: oracle_text diverges (similarity 0.26); scryfall='Whenever you gain life, put a +1/+1 counter on this creature.
As long as this creature has four or more +1/+1
- oracle_text advisory -- Daxos, Blessed by the Sun: oracle_text diverges (similarity 0.18); scryfall="Daxos's toughness is equal to your devotion to white. (Each {W} in the mana costs of permanents you control co
- oracle_text advisory -- Archangel of Thune: oracle_text diverges (similarity 0.24); scryfall='Flying
Lifelink (Damage dealt by this creature also causes you to gain that much life.)
Whenever you gain li
- oracle_text advisory -- Heliod, Sun-Crowned: oracle_text diverges (similarity 0.13); scryfall="Indestructible
As long as your devotion to white is less than five, Heliod isn't a creature.
Whenever you ga
- oracle_text advisory -- Ranger-Captain of Eos: oracle_text diverges (similarity 0.13); scryfall="When this creature enters, you may search your library for a creature card with mana value 1 or less, reveal i
- oracle_text advisory -- Ajani, Strength of the Pride: oracle_text diverges (similarity 0.27); scryfall='+1: You gain life equal to the number of creatures you control plus the number of planeswalkers you control.

- oracle_text advisory -- Apex Altisaur: oracle_text diverges (similarity 0.21); scryfall="When this creature enters, it fights up to one target creature you don't control.
Enrage — Whenever this crea
- oracle_text advisory -- World War Hulk: oracle_text diverges (similarity 0.17); scryfall='(As this Saga enters and after your draw step, add a lore counter. Sacrifice after III.)
I — The next red or 
- oracle_text advisory -- Ghalta, Stampede Tyrant: oracle_text diverges (similarity 0.17); scryfall='Trample
When Ghalta enters, put any number of creature cards from your hand onto the battlefield.'
- oracle_text advisory -- Lyra Dawnbringer: oracle_text diverges (similarity 0.11); scryfall='Flying
First strike (This creature deals combat damage before creatures without first strike.)
Lifelink (Dam
- oracle_text advisory -- Youthful Valkyrie: oracle_text diverges (similarity 0.08); scryfall='Flying
Whenever another Angel you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Bishop of Wings: oracle_text diverges (similarity 0.10); scryfall='Whenever an Angel you control enters, you gain 4 life.
Whenever an Angel you control dies, create a 1/1 white
- oracle_text advisory -- Righteous Valkyrie: oracle_text diverges (similarity 0.15); scryfall="Flying
Whenever another Angel or Cleric you control enters, you gain life equal to that creature's toughness.
- oracle_text advisory -- Seraph Sanctuary: oracle_text diverges (similarity 0.12); scryfall='When this land enters, you gain 1 life.
Whenever an Angel you control enters, you gain 1 life.
{T}: Add {C}.
- oracle_text advisory -- Resplendent Angel: oracle_text diverges (similarity 0.11); scryfall='Flying
At the beginning of each end step, if you gained 5 or more life this turn, create a 4/4 white Angel cr
- oracle_text advisory -- Legion Angel: oracle_text diverges (similarity 0.07); scryfall='Flying
When this creature enters, you may reveal a card you own named Legion Angel from outside the game and 
- oracle_text advisory -- Serra the Benevolent: oracle_text diverges (similarity 0.16); scryfall='+2: Creatures you control with flying get +1/+1 until end of turn.
−3: Create a 4/4 white Angel creature toke
- oracle_text advisory -- Giada, Font of Hope: oracle_text diverges (similarity 0.11); scryfall='Flying, vigilance
Each other Angel you control enters with an additional +1/+1 counter on it for each Angel y
- oracle_text advisory -- Saproling Burst: oracle_text diverges (similarity 0.13); scryfall='Fading 7 (This enchantment enters with seven fade counters on it. At the beginning of your upkeep, remove a fa
- oracle_text advisory -- Shroofus Sproutsire: oracle_text diverges (similarity 0.06); scryfall='Trample
Whenever a Saproling you control deals combat damage to a player, create that many 1/1 green Saprolin
- oracle_text advisory -- Slimefoot, the Stowaway: oracle_text diverges (similarity 0.05); scryfall='Whenever a Saproling you control dies, Slimefoot deals 1 damage to each opponent and you gain 1 life.
{4}: Cr
- oracle_text advisory -- Vitaspore Thallid: oracle_text diverges (similarity 0.11); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this 
- oracle_text advisory -- Deathspore Thallid: oracle_text diverges (similarity 0.10); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this 
- oracle_text advisory -- Sporecrown Thallid: oracle_text diverges (similarity 0.22); scryfall="Each other creature you control that's a Fungus or Saproling gets +1/+1."
- oracle_text advisory -- Tukatongue Thallid: oracle_text diverges (similarity 0.13); scryfall='When this creature dies, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- Yavimaya Sapherd: oracle_text diverges (similarity 0.06); scryfall='When this creature enters, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- Deathbloom Thallid: oracle_text diverges (similarity 0.05); scryfall='When this creature dies, create a 1/1 green Saproling creature token.'
- oracle_text advisory -- The Mycotyrant: oracle_text diverges (similarity 0.18); scryfall='Trample
The Mycotyrant\'s power and toughness are each equal to the number of creatures you control that are 
- oracle_text advisory -- Simic Growth Chamber: oracle_text diverges (similarity 0.21); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {G}{
- oracle_text advisory -- Psychotrope Thallid: oracle_text diverges (similarity 0.22); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this 
- oracle_text advisory -- Concordant Crossroads: oracle_text diverges (similarity 0.01); scryfall='All creatures have haste.'
- oracle_text advisory -- Brightcap Badger: oracle_text diverges (similarity 0.12); scryfall='Each Fungus and Saproling you control has "{T}: Add {G}."
At the beginning of your end step, create a 1/1 gre
- oracle_text advisory -- Fungus Frolic: oracle_text diverges (similarity 0.10); scryfall='Create two 1/1 green Saproling creature tokens. (Then exile this card. You may cast the creature later from ex
- oracle_text advisory -- Spore Swarm: oracle_text diverges (similarity 0.06); scryfall='Create three 1/1 green Saproling creature tokens.'
- oracle_text advisory -- Saproling Migration: oracle_text diverges (similarity 0.13); scryfall='Kicker {4} (You may pay an additional {4} as you cast this spell.)
Create two 1/1 green Saproling creature to
- oracle_text advisory -- Doubling Season: oracle_text diverges (similarity 0.28); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Parallel Lives: oracle_text diverges (similarity 0.13); scryfall='If an effect would create one or more tokens under your control, it creates twice that many of those tokens in
- oracle_text advisory -- Primal Vigor: oracle_text diverges (similarity 0.15); scryfall='If one or more tokens would be created, twice that many of those tokens are created instead.
If one or more +
- oracle_text advisory -- Beastmaster Ascension: oracle_text diverges (similarity 0.17); scryfall='Whenever a creature you control attacks, you may put a quest counter on this enchantment.
As long as this enc
- oracle_text advisory -- Thallid: oracle_text diverges (similarity 0.17); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this 
- oracle_text advisory -- Thallid Shell-Dweller: oracle_text diverges (similarity 0.13); scryfall='Defender
At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters 
- oracle_text advisory -- Sporesower Thallid: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on each Fungus you control.
Remove three spore counters 
- oracle_text advisory -- Utopia Mycon: oracle_text diverges (similarity 0.12); scryfall='At the beginning of your upkeep, put a spore counter on this creature.
Remove three spore counters from this 
- oracle_text advisory -- Mycoloth: oracle_text diverges (similarity 0.14); scryfall='Devour 2 (As this creature enters, you may sacrifice any number of creatures. It enters with twice that many +
- oracle_text advisory -- Lightstall Inquisitor: oracle_text diverges (similarity 0.17); scryfall='Vigilance
When this creature enters, each opponent exiles a card from their hand and may play that card for a
- oracle_text advisory -- Lyra, Archangel of Dawn: oracle_text diverges (similarity 0.06); scryfall='Flying
Whenever you gain life, put a +1/+1 counter on each Angel you control.'
- oracle_text advisory -- Sunrise Sovereign: oracle_text diverges (similarity 0.10); scryfall='Other Giant creatures you control get +2/+2 and have trample.'
- oracle_text advisory -- Stinkdrinker Daredevil: oracle_text diverges (similarity 0.11); scryfall='Giant spells you cast cost {2} less to cast.'
- oracle_text advisory -- Giant Harbinger: oracle_text diverges (similarity 0.14); scryfall='When this creature enters, you may search your library for a Giant card, reveal it, then shuffle and put that 
- oracle_text advisory -- Pyroclasm: oracle_text diverges (similarity 0.05); scryfall='Pyroclasm deals 2 damage to each creature.'
- oracle_text advisory -- Hamletback Goliath: oracle_text diverges (similarity 0.09); scryfall="Whenever another creature enters, you may put X +1/+1 counters on this creature, where X is that creature's po
- oracle_text advisory -- Borderland Behemoth: oracle_text diverges (similarity 0.06); scryfall='Trample
This creature gets +4/+4 for each other Giant you control.'
- oracle_text advisory -- Inferno Titan: oracle_text diverges (similarity 0.13); scryfall='{R}: This creature gets +1/+0 until end of turn.
Whenever this creature enters or attacks, it deals 3 damage 
- oracle_text advisory -- Surtland Flinger: oracle_text diverges (similarity 0.18); scryfall="Whenever this creature attacks, you may sacrifice another creature. When you do, this creature deals damage eq
- oracle_text advisory -- Tectonic Giant: oracle_text diverges (similarity 0.16); scryfall='Whenever this creature attacks or becomes the target of a spell an opponent controls, choose one —
• This cre
- oracle_text advisory -- Metallic Mimic: oracle_text diverges (similarity 0.18); scryfall='As this creature enters, choose a creature type.
This creature is the chosen type in addition to its other ty
- oracle_text advisory -- Adaptive Automaton: oracle_text diverges (similarity 0.23); scryfall='As this creature enters, choose a creature type.
This creature is the chosen type in addition to its other ty
- oracle_text advisory -- Corsair Captain: oracle_text diverges (similarity 0.18); scryfall='When this creature enters, create a Treasure token. (It\'s an artifact with "{T}, Sacrifice this token: Add on
- oracle_text advisory -- Dire Fleet Captain: oracle_text diverges (similarity 0.17); scryfall='Whenever this creature attacks, it gets +1/+1 until end of turn for each other attacking Pirate.'
- oracle_text advisory -- Goblin Tomb Raider: oracle_text diverges (similarity 0.09); scryfall='As long as you control an artifact, this creature gets +1/+0 and has haste.'
- oracle_text advisory -- Forerunner of the Coalition: oracle_text diverges (similarity 0.22); scryfall='When this creature enters, you may search your library for a Pirate card, reveal it, then shuffle and put that
- oracle_text advisory -- Staunch Crewmate: oracle_text diverges (similarity 0.33); scryfall='When this creature enters, look at the top four cards of your library. You may reveal an artifact or Pirate ca
- oracle_text advisory -- Malcolm, the Eyes: oracle_text diverges (similarity 0.26); scryfall='Flying, haste
Whenever you cast your second spell each turn, investigate. (Create a Clue token. It\'s an arti
- oracle_text advisory -- Siren Stormtamer: oracle_text diverges (similarity 0.16); scryfall='Flying
{U}, Sacrifice this creature: Counter target spell or ability that targets you or a creature you contr
- oracle_text advisory -- Daring Buccaneer: oracle_text diverges (similarity 0.09); scryfall='As an additional cost to cast this spell, reveal a Pirate card from your hand or pay {2}.'
- oracle_text advisory -- Kitesail Larcenist: oracle_text diverges (similarity 0.17); scryfall='Flying, ward {1}
When this creature enters, for each player, choose up to one other target artifact or creatu
- oracle_text advisory -- Spirebluff Canal: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.
{T}: Add {U} or {R}.'
- oracle_text advisory -- Blackcleave Cliffs: oracle_text diverges (similarity 0.33); scryfall='This land enters tapped unless you control two or fewer other lands.
{T}: Add {B} or {R}.'
- oracle_text advisory -- Battlefield Forge: oracle_text diverges (similarity 0.18); scryfall='{T}: Add {C}.
{T}: Add {R} or {W}. This land deals 1 damage to you.'
- oracle_text advisory -- Karplusan Forest: oracle_text diverges (similarity 0.43); scryfall='{T}: Add {C}.
{T}: Add {R} or {G}. This land deals 1 damage to you.'
- oracle_text advisory -- Tarnished Citadel: oracle_text diverges (similarity 0.28); scryfall='{T}: Add {C}.
{T}: Add one mana of any color. This land deals 3 damage to you.'
- oracle_text advisory -- Grand Coliseum: oracle_text diverges (similarity 0.53); scryfall='This land enters tapped.
{T}: Add {C}.
{T}: Add one mana of any color. This land deals 1 damage to you.'
- oracle_text advisory -- Ancient Tomb: oracle_text diverges (similarity 0.17); scryfall='{T}: Add {C}{C}. This land deals 2 damage to you.'
- oracle_text advisory -- Manabarbs: oracle_text diverges (similarity 0.12); scryfall='Whenever a player taps a land for mana, this enchantment deals 1 damage to that player.'
- oracle_text advisory -- Tamanoa: oracle_text diverges (similarity 0.13); scryfall='Whenever a noncreature source you control deals damage, you gain that much life.'
- oracle_text advisory -- Rhox Faithmender: oracle_text diverges (similarity 0.37); scryfall='Lifelink (Damage dealt by this creature also causes you to gain that much life.)
If you would gain life, you 
- oracle_text advisory -- Vito, Thorn of the Dusk Rose: oracle_text diverges (similarity 0.16); scryfall='Whenever you gain life, target opponent loses that much life.
{3}{B}{B}: Creatures you control gain lifelink 
- oracle_text advisory -- Dina, Soul Steeper: oracle_text diverges (similarity 0.21); scryfall="Whenever you gain life, each opponent loses 1 life.
{1}, Sacrifice another creature: Dina gets +X/+0 until en
- oracle_text advisory -- Bilbo, Birthday Celebrant: oracle_text diverges (similarity 0.24); scryfall='If you would gain life, you gain that much life plus 1 instead.
{2}{W}{B}{G}, {T}, Exile Bilbo: Search your l
- oracle_text advisory -- Purity: oracle_text diverges (similarity 0.24); scryfall="Flying
If noncombat damage would be dealt to you, prevent that damage. You gain life equal to the damage prev
- oracle_text advisory -- Spellshock: oracle_text diverges (similarity 0.09); scryfall='Whenever a player casts a spell, this enchantment deals 2 damage to that player.'
- oracle_text advisory -- Pyrohemia: oracle_text diverges (similarity 0.11); scryfall='At the beginning of the end step, if no creatures are on the battlefield, sacrifice this enchantment.
{R}: Th
- oracle_text advisory -- Rolling Earthquake: oracle_text diverges (similarity 0.08); scryfall='Rolling Earthquake deals X damage to each creature without horsemanship and each player.'
- oracle_text advisory -- Beseech the Queen: oracle_text diverges (similarity 0.20); scryfall="({2/B} can be paid with any two mana or with {B}. This card's mana value is 6.)
Search your library for a car
- oracle_text advisory -- Green Sun's Zenith: oracle_text diverges (similarity 0.17); scryfall="Search your library for a green creature card with mana value X or less, put it onto the battlefield, then shu
- oracle_text advisory -- Dimir House Guard: oracle_text diverges (similarity 0.25); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)
Sacrifice a creatu
- oracle_text advisory -- Shriekmaw: oracle_text diverges (similarity 0.26); scryfall="Fear (This creature can't be blocked except by artifact creatures and/or black creatures.)
When this creature
- oracle_text advisory -- Timeless Witness: oracle_text diverges (similarity 0.29); scryfall="When this creature enters, return target card from your graveyard to your hand.
Eternalize {5}{G}{G} ({5}{G}{
- oracle_text advisory -- Acidic Slime: oracle_text diverges (similarity 0.25); scryfall='Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)
When this creature enters
- oracle_text advisory -- Ageless Entity: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, put that many +1/+1 counters on this creature.'
- oracle_text advisory -- Selesnya Sanctuary: oracle_text diverges (similarity 0.06); scryfall="This land enters tapped.
When this land enters, return a land you control to its owner's hand.
{T}: Add {G}{
- oracle_text advisory -- Blossoming Sands: oracle_text diverges (similarity 0.06); scryfall='This land enters tapped.
When this land enters, you gain 1 life.
{T}: Add {G} or {W}.'
- oracle_text advisory -- Verdant Sun's Avatar: oracle_text diverges (similarity 0.05); scryfall="Whenever this creature or another creature you control enters, you gain life equal to that creature's toughnes
- oracle_text advisory -- Feed the Clan: oracle_text diverges (similarity 0.04); scryfall='You gain 5 life.
Ferocious — You gain 10 life instead if you control a creature with power 4 or greater.'
- oracle_text advisory -- Accomplished Alchemist: oracle_text diverges (similarity 0.04); scryfall='{T}: Add one mana of any color.
{T}: Add X mana of any one color, where X is the amount of life you gained th
- oracle_text advisory -- Blighted Steppe: oracle_text diverges (similarity 0.04); scryfall='{T}: Add {C}.
{3}{W}, {T}, Sacrifice this land: You gain 2 life for each creature you control.'
- oracle_text advisory -- Wellwisher: oracle_text diverges (similarity 0.02); scryfall='{T}: You gain 1 life for each Elf on the battlefield.'
- oracle_text advisory -- Nykthos Paragon: oracle_text diverges (similarity 0.03); scryfall='Whenever you gain life, you may put that many +1/+1 counters on each creature you control. Do this only once e
- oracle_text advisory -- Blossoming Bogbeast: oracle_text diverges (similarity 0.05); scryfall='Whenever this creature attacks, you gain 2 life. Then creatures you control gain trample and get +X/+X until e
- oracle_text advisory -- Genesis Wave: oracle_text diverges (similarity 0.07); scryfall="Reveal the top X cards of your library. You may put any number of permanent cards with mana value X or less fr
- oracle_text advisory -- Champion of the Parish: oracle_text diverges (similarity 0.11); scryfall='Whenever another Human you control enters, put a +1/+1 counter on this creature.'
- oracle_text advisory -- Field Marshal: oracle_text diverges (similarity 0.12); scryfall='Other Soldier creatures get +1/+1 and have first strike. (They deal combat damage before creatures without fir
- oracle_text advisory -- Thalia, Guardian of Thraben: oracle_text diverges (similarity 0.06); scryfall='First strike
Noncreature spells cost {1} more to cast.'
- oracle_text advisory -- Thalia's Lieutenant: oracle_text diverges (similarity 0.12); scryfall='When this creature enters, put a +1/+1 counter on each other Human you control.
Whenever another Human you co
- oracle_text advisory -- Rick, Steadfast Leader: oracle_text diverges (similarity 0.16); scryfall="As Greymond, Avacyn's Stalwart enters, choose two abilities from among first strike, vigilance, and lifelink.\
- oracle_text advisory -- Esper Sentinel: oracle_text diverges (similarity 0.14); scryfall="Whenever an opponent casts their first noncreature spell each turn, draw a card unless that player pays {X}, w
- oracle_text advisory -- Coppercoat Vanguard: oracle_text diverges (similarity 0.17); scryfall='Each other Human you control gets +1/+0 and has ward {1}. (Whenever it becomes the target of a spell or abilit
- oracle_text advisory -- Recruitment Officer: oracle_text diverges (similarity 0.17); scryfall='{3}{W}: Look at the top four cards of your library. You may reveal a creature card with mana value 3 or less f
- oracle_text advisory -- Jirina, Dauntless General: oracle_text diverges (similarity 0.09); scryfall="When Jirina enters, exile target player's graveyard.
Sacrifice Jirina: Humans you control gain hexproof and i
- oracle_text advisory -- Harbin, Vanguard Aviator: oracle_text diverges (similarity 0.08); scryfall='Flying
Whenever you attack with five or more Soldiers, creatures you control get +1/+1 and gain flying until 
- oracle_text advisory -- General Kudro of Drannith: oracle_text diverges (similarity 0.19); scryfall="Other Humans you control get +1/+1.
Whenever General Kudro or another Human you control enters, exile target 
- oracle_text advisory -- Cathar Commando: oracle_text diverges (similarity 0.06); scryfall='Flash
{1}, Sacrifice this creature: Destroy target artifact or enchantment.'
- oracle_text advisory -- Brutal Cathar: oracle_text diverges (similarity 0.10); scryfall='Whenever this creature enters or transforms into Brutal Cathar, exile target creature an opponent controls unt
- oracle_text advisory -- Moonrage Brute: oracle_text diverges (similarity 0.22); scryfall='First strike
Ward—Pay 3 life.
Nightbound (If a player casts at least two spells during their own turn, it be
- oracle_text advisory -- King Darien XLVIII: oracle_text diverges (similarity 0.17); scryfall='Other creatures you control get +1/+1.
{3}{G}{W}: Put a +1/+1 counter on King Darien and create a 1/1 white S
- oracle_text advisory -- Fortified Beachhead: oracle_text diverges (similarity 0.26); scryfall='As this land enters, you may reveal a Soldier card from your hand. This land enters tapped unless you revealed
- oracle_text advisory -- Silent Clearing: oracle_text diverges (similarity 0.10); scryfall='{T}, Pay 1 life: Add {W} or {B}.
{1}, {T}, Sacrifice this land: Draw a card.'
- oracle_text advisory -- Adelbert Steiner: oracle_text diverges (similarity 0.02); scryfall='Lifelink
Adelbert Steiner gets +1/+1 for each Equipment you control.'
- oracle_text advisory -- Adaptive Omnitool: oracle_text diverges (similarity 0.07); scryfall='Equipped creature gets +1/+1 for each artifact you control.
Whenever equipped creature attacks, look at the t
- oracle_text advisory -- Kíli the Resourceful: oracle_text diverges (similarity 0.07); scryfall='Storied (If you control three or more artifacts, legendaries, and/or Sagas, you have an enduring story for the
- oracle_text advisory -- Almost Perfect: oracle_text diverges (similarity 0.20); scryfall='Enchant creature
Enchanted creature has base power and toughness 9/10 and has indestructible.'
- oracle_text advisory -- Arcanum Wings: oracle_text diverges (similarity 0.15); scryfall='Enchant creature
Enchanted creature has flying.
Aura swap {2}{U} ({2}{U}: Exchange this Aura with an Aura ca
- oracle_text advisory -- Boseiju, Who Endures: oracle_text diverges (similarity 0.38); scryfall='{T}: Add {G}.
Channel — {1}{G}, Discard this card: Destroy target artifact, enchantment, or nonbasic land an 
- oracle_text advisory -- Botanical Sanctum: oracle_text diverges (similarity 0.29); scryfall='This land enters tapped unless you control two or fewer other lands.
{T}: Add {G} or {U}.'
- oracle_text advisory -- Bruna, Light of Alabaster: oracle_text diverges (similarity 0.24); scryfall='Flying, vigilance
Whenever Bruna attacks or blocks, you may attach to it any number of Auras on the battlefie
- oracle_text advisory -- Colossification: oracle_text diverges (similarity 0.14); scryfall='Enchant creature
When this Aura enters, tap enchanted creature.
Enchanted creature gets +20/+20.'
- oracle_text advisory -- Eldrazi Conscription: oracle_text diverges (similarity 0.39); scryfall='Enchant creature
Enchanted creature gets +10/+10 and has trample and annihilator 2. (Whenever it attacks, def
- oracle_text advisory -- Glittering Wish: oracle_text diverges (similarity 0.06); scryfall='You may reveal a multicolored card you own from outside the game and put it into your hand. Exile Glittering W
- oracle_text advisory -- Indrik Umbra: oracle_text diverges (similarity 0.38); scryfall='Enchant creature
Enchanted creature gets +4/+4 and has first strike, and all creatures able to block it do so
- oracle_text advisory -- Mother of Runes: oracle_text diverges (similarity 0.13); scryfall='{T}: Target creature you control gains protection from the color of your choice until end of turn.'
- oracle_text advisory -- Mythic Proportions: oracle_text diverges (similarity 0.40); scryfall='Enchant creature
Enchanted creature gets +8/+8 and has trample.'
- oracle_text advisory -- Open the Armory: oracle_text diverges (similarity 0.21); scryfall='Search your library for an Aura or Equipment card, reveal it, put it into your hand, then shuffle.'
- oracle_text advisory -- Prodigious Growth: oracle_text diverges (similarity 0.26); scryfall='Enchant creature
Enchanted creature gets +7/+7 and has trample.'
- oracle_text advisory -- Seaside Citadel: oracle_text diverges (similarity 0.15); scryfall='This land enters tapped.
{T}: Add {G}, {W}, or {U}.'
- oracle_text advisory -- Skycloud Expanse: oracle_text diverges (similarity 0.06); scryfall='{1}, {T}: Add {W}{U}.'
- oracle_text advisory -- Somberwald Sage: oracle_text diverges (similarity 0.12); scryfall='{T}: Add three mana of any one color. Spend this mana only to cast creature spells.'
- oracle_text advisory -- Unflinching Courage: oracle_text diverges (similarity 0.43); scryfall='Enchant creature
Enchanted creature gets +2/+2 and has trample and lifelink. (Damage dealt by the creature al
- oracle_text advisory -- Linvala, Shield of Sea Gate: oracle_text diverges (similarity 0.31); scryfall="Flying
At the beginning of combat on your turn, if you have a full party, choose target nonland permanent an 
- oracle_text advisory -- Troyan, Gutsy Explorer: oracle_text diverges (similarity 0.15); scryfall='{T}: Add {G}{U}. Spend this mana only to cast spells with mana value 5 or greater or spells with {X} in their 
- oracle_text advisory -- Detention Sphere: oracle_text diverges (similarity 0.25); scryfall="When this enchantment enters, you may exile target nonland permanent not named Detention Sphere and all other 
- oracle_text advisory -- Steel of the Godhead: oracle_text diverges (similarity 0.31); scryfall="Enchant creature
As long as enchanted creature is white, it gets +1/+1 and has lifelink. (Damage dealt by the
- oracle_text advisory -- Auroral Procession: oracle_text diverges (similarity 0.07); scryfall='Return target card from your graveyard to your hand.'
- oracle_text advisory -- Reborn Hope: oracle_text diverges (similarity 0.10); scryfall='Return target multicolored card from your graveyard to your hand.'
- oracle_text advisory -- Rampaging Baloths: oracle_text diverges (similarity 0.14); scryfall='Trample
Landfall — Whenever a land you control enters, create a 4/4 green Beast creature token.'
- oracle_text advisory -- Avenger of Zendikar: oracle_text diverges (similarity 0.21); scryfall='When this creature enters, create a 0/1 green Plant creature token for each land you control.
Landfall — When
- oracle_text advisory -- Omnath, Locus of Rage: oracle_text diverges (similarity 0.20); scryfall='Landfall — Whenever a land you control enters, create a 5/5 red and green Elemental creature token.
Whenever 
- oracle_text advisory -- Phylath, World Sculptor: oracle_text diverges (similarity 0.19); scryfall='When Phylath enters, create a 0/1 green Plant creature token for each basic land you control.
Landfall — When
- oracle_text advisory -- Courser of Kruphix: oracle_text diverges (similarity 0.17); scryfall='Play with the top card of your library revealed.
You may play lands from the top of your library.
Landfall —
- oracle_text advisory -- Khalni Heart Expedition: oracle_text diverges (similarity 0.19); scryfall='Landfall — Whenever a land you control enters, you may put a quest counter on this enchantment.
Remove three 
- oracle_text advisory -- Rampant Growth: oracle_text diverges (similarity 0.16); scryfall='Search your library for a basic land card, put that card onto the battlefield tapped, then shuffle.'
- oracle_text advisory -- Search for Tomorrow: oracle_text diverges (similarity 0.26); scryfall='Search your library for a basic land card, put it onto the battlefield, then shuffle.
Suspend 2—{G} (Rather t
- oracle_text advisory -- Cultivate: oracle_text diverges (similarity 0.24); scryfall='Search your library for up to two basic land cards, reveal those cards, put one onto the battlefield tapped an
- oracle_text advisory -- Evolving Wilds: oracle_text diverges (similarity 0.11); scryfall='{T}, Sacrifice this land: Search your library for a basic land card, put it onto the battlefield tapped, then 
- oracle_text advisory -- Terramorphic Expanse: oracle_text diverges (similarity 0.11); scryfall='{T}, Sacrifice this land: Search your library for a basic land card, put it onto the battlefield tapped, then 
- oracle_text advisory -- Myriad Landscape: oracle_text diverges (similarity 0.25); scryfall='This land enters tapped.
{T}: Add {C}.
{2}, {T}, Sacrifice this land: Search your library for up to two basi
- oracle_text advisory -- Primeval Titan: oracle_text diverges (similarity 0.16); scryfall='Trample
Whenever this creature enters or attacks, you may search your library for up to two land cards, put t
- oracle_text advisory -- Radiant Fountain: oracle_text diverges (similarity 0.12); scryfall='When this land enters, you gain 2 life.
{T}: Add {C}.'
- oracle_text advisory -- Haunted Fengraf: oracle_text diverges (similarity 0.12); scryfall='{T}: Add {C}.
{3}, {T}, Sacrifice this land: Return a creature card at random from your graveyard to your han
- oracle_text advisory -- Temple of Deceit: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Enlightenment: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Malady: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Malice: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Mystery: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Plenty: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Temple of Triumph: oracle_text diverges (similarity 0.52); scryfall='This land enters tapped.
When this land enters, scry 1. (Look at the top card of your library. You may put th
- oracle_text advisory -- Zhalfirin Void: oracle_text diverges (similarity 0.74); scryfall='When this land enters, scry 1. (Look at the top card of your library. You may put that card on the bottom.)
{
- oracle_text advisory -- Crystal Grotto: oracle_text diverges (similarity 0.39); scryfall='When this land enters, scry 1.
{T}: Add {C}.
{1}, {T}: Add one mana of any color.'
- oracle_text advisory -- Rumble Arena: oracle_text diverges (similarity 0.44); scryfall='Vigilance
When this land enters, scry 1. (Look at the top card of your library. You may put it on the bottom.
- oracle_text advisory -- The Grey Havens: oracle_text diverges (similarity 0.24); scryfall='When The Grey Havens enters, scry 1.
{T}: Add {C}.
{T}: Add one mana of any color among legendary creature c
- oracle_text advisory -- Galadriel of Lothlórien: oracle_text diverges (similarity 0.24); scryfall='Whenever the Ring tempts you, if you chose a creature other than Galadriel as your Ring-bearer, scry 3.
Whene
- oracle_text advisory -- Clifftop Retreat: oracle_text diverges (similarity 0.37); scryfall='This land enters tapped unless you control a Mountain or a Plains.
{T}: Add {R} or {W}.'
- oracle_text advisory -- Furycalm Snarl: oracle_text diverges (similarity 0.31); scryfall="As this land enters, you may reveal a Mountain or Plains card from your hand. If you don't, this land enters t
- oracle_text advisory -- Steelshaper's Gift: oracle_text diverges (similarity 0.18); scryfall='Search your library for an Equipment card, reveal that card, put it into your hand, then shuffle.'
- oracle_text advisory -- Swiftblade Vindicator: oracle_text diverges (similarity 0.27); scryfall="Double strike (This creature deals both first-strike and regular combat damage.)
Vigilance (Attacking doesn't
- oracle_text advisory -- Lizard Blades: oracle_text diverges (similarity 0.11); scryfall="Double strike
Equipped creature has double strike.
Reconfigure {2} ({2}: Attach to target creature you contr
- oracle_text advisory -- Magnetic Theft: oracle_text diverges (similarity 0.04); scryfall="Attach target Equipment to target creature. (Control of the Equipment doesn't change.)"
- oracle_text advisory -- Forge Anew: oracle_text diverges (similarity 0.09); scryfall='When this enchantment enters, return target Equipment card from your graveyard to the battlefield.
During you
- oracle_text advisory -- Raubahn, Bull of Ala Mhigo: oracle_text diverges (similarity 0.07); scryfall="Ward—Pay life equal to Raubahn's power.
Whenever Raubahn attacks, attach up to one target Equipment you contr
- oracle_text advisory -- Speaker of the Heavens: oracle_text diverges (similarity 0.16); scryfall="Vigilance, lifelink (Attacking doesn't cause this creature to tap. Damage dealt by this creature also causes y
- oracle_text advisory -- Path to Exile: oracle_text diverges (similarity 0.12); scryfall='Exile target creature. Its controller may search their library for a basic land card, put that card onto the b
- oracle_text advisory -- Legion Lieutenant: oracle_text diverges (similarity 0.11); scryfall='Other Vampires you control get +1/+1.'
- oracle_text advisory -- Vampire of the Dire Moon: oracle_text diverges (similarity 0.37); scryfall='Deathtouch (Any amount of damage this deals to a creature is enough to destroy it.)
Lifelink (Damage dealt by
- oracle_text advisory -- Falkenrath Pit Fighter: oracle_text diverges (similarity 0.11); scryfall='{1}{R}, Discard a card, Sacrifice a Vampire: Draw two cards. Activate only if an opponent lost life this turn.
- oracle_text advisory -- Stromkirk Condemned: oracle_text diverges (similarity 0.15); scryfall='Discard a card: Vampires you control get +1/+1 until end of turn. Activate only once each turn.'
- oracle_text advisory -- Voldaren Estate: oracle_text diverges (similarity 0.35); scryfall='{T}: Add {C}.
{T}, Pay 1 life: Add one mana of any color. Spend this mana only to cast a Vampire spell.
{5},
- oracle_text advisory -- Bloodghast: oracle_text diverges (similarity 0.19); scryfall="This creature can't block.
This creature has haste as long as an opponent has 10 or less life.
Landfall — Wh
- oracle_text advisory -- Stromkirk Captain: oracle_text diverges (similarity 0.11); scryfall='First strike
Other Vampire creatures you control get +1/+1 and have first strike.'
- oracle_text advisory -- Drana, Liberator of Malakir: oracle_text diverges (similarity 0.14); scryfall='Flying, first strike
Whenever Drana deals combat damage to a player, put a +1/+1 counter on each attacking cr
- oracle_text advisory -- Mavren Fein, Dusk Apostle: oracle_text diverges (similarity 0.14); scryfall='Whenever one or more nontoken Vampires you control attack, create a 1/1 white Vampire creature token with life
- oracle_text advisory -- Vampire Socialite: oracle_text diverges (similarity 0.26); scryfall="Menace (This creature can't be blocked except by two or more creatures.)
When this creature enters, if an opp
- oracle_text advisory -- Falkenrath Gorger: oracle_text diverges (similarity 0.27); scryfall="Each Vampire creature card you own that isn't on the battlefield has madness. The madness cost is equal to its
- oracle_text advisory -- Markov Baron: oracle_text diverges (similarity 0.26); scryfall="Convoke (Your creatures can help cast this spell. Each creature you tap while casting this spell pays for {1} 
- oracle_text advisory -- Asylum Visitor: oracle_text diverges (similarity 0.33); scryfall="At the beginning of each player's upkeep, if that player has no cards in hand, you draw a card and you lose 1 
- oracle_text advisory -- Captivating Vampire: oracle_text diverges (similarity 0.12); scryfall='Other Vampire creatures you control get +1/+1.
Tap five untapped Vampires you control: Gain control of target
- oracle_text advisory -- Bloodline Keeper: oracle_text diverges (similarity 0.12); scryfall='Flying
{T}: Create a 2/2 black Vampire creature token with flying.
{B}: Transform this creature. Activate on
- oracle_text advisory -- Lord of Lineage: oracle_text diverges (similarity 0.17); scryfall='Flying
Other Vampire creatures you control get +2/+2.
{T}: Create a 2/2 black Vampire creature token with fl
- card_fields snapshot INCOMPLETE -- 1 card(s) not in the snapshot; run --update to reality-check them: Temple of Abandon
- clause_ledger: no dedicated per-clause artifact. Its function -- every oracle clause modeled/inert/deferred -- is covered by coverage(partial hard-stop) + bracket-note deferrals + viewer oracle cross-check + audit_card_fields oracle-diff. A dedicated ledger is deferred (high per-card cost, marginal added rigor).
- claude_sweep recorded at commit 2ed53cf74 (HEAD 8ca366dc8a32); re-run if play changed since (play_invariants + smoke digests track play live).

<!-- verify_deck:end -->
