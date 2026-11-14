# Handoff: the mana-clean fix chain, Claude session -> Codex (2026-11-14 ~10:45 UTC)

The Claude owner session (24b9e9dc) is out of usage and hands the USER's standing mana-clean program to Codex.
Detailed per-item history is in `docs/design/mana-clean-pass-checkpoint.md` (newest bullets at the end of each
round). Per-item handoff files (gitignored, local only) are in `logs/mana-clean-chain/<item>-handoff.md`.

## The program (USER, standing)
Run every deck through `python3 scripts/mana_clean.py run --games 5000` (always pass `--games 5000`), fix every issue,
mark each deck. A deck leaves beta only after a clean analysis of >= 5,000 games on a FRESH seed block run after its
fixes (`python3 scripts/mana_clean.py status` lists each deck's next block). Fixes are mechanism-level, LOSSLESS
(everything findable at unlimited budget) and NO-GREEDY. Work items serially, one engine job at a time
(`MTG_RSS_CAP_GB=40`). Adoption bar for a play change: units, repros, `bash test/regression.sh --smoke` and
`--regression` (note the `--` flag form), a pooled held-out A/B with each deck's net <= 0 and every slower game
recovering at `--depth 8 --budget-ms 0`, `python3 test/check_gt_logs.py`. No overnight tier. Gates use `./build.sh pgo`.

## Branch state at handoff
- `phase-1-2-deck-analyzer`: last pushed by this session at ~08:00 UTC (246e34da6..78ba73d2d); local is ahead by doc
  commits only (item E checkpoint bullet, this file). The next shared push by this session's rhythm would be >= 12:00 UTC.
- CI for that push: run 43573881414 was cancelled by another session's push; follow-up run 43578742559 includes it:
  Windows build green; ubuntu + determinism parity had not landed at handoff. Check `gh run view 43578742559`.
- `wip/item-W-merge` (local): item W WIP (see below). `wip/gods-gy-option-demoted` 847943ebf (pushed): item E.

## CI: ubuntu hang in the viewer line-build check (check FIRST)
Runs 43573881414 and 43578742559 (both contain this chain's push 246e34da6..78ba73d2d) were cancelled by newer pushes,
and in the second the ubuntu job sat in `node test/viewer_linebuild_check.js` for 3 h 12 m after printing all 22
sub-checks (0 FAIL, 835 references). The last green run 43564679055 (before the push) ran that step in 1 s on the same
runner image; locally on the current tree the script exits 0 in 4 s (`process.exit(main())`, no child processes), and
the pushed range touches none of its inputs. So determinism parity has NOT run for this code yet. Newest run
43596869487 was being watched (log `build/scratch/claude1110/ci-push3c.log`). If ubuntu hangs in that step again it
is a repeatable Linux CI hang to root-cause. Open USER question: add `timeout-minutes` to that workflow step? (The
repo's no-timeout rule covers commands agents run; a CI step limit is the USER's call -- not changed.)

## RUNNING / IN PROGRESS at handoff
**Item W -- wild-mana prepay is lossy in places** (from the Spirits session's
`docs/design/handoff-mana-overnight-2026-11-14.md`; its status section is to be appended when W resolves).
Handoff: `logs/mana-clean-chain/W-handoff.md` (section #2 is the latest). **Current WIP: `wip/item-W-defer`
5571c1edc** (`wip/item-W-merge` points at the same commit). The earlier merge approach was DROPPED (it floated a
two-colour wild unit, against the USER ruling below, and still lost 3+-colour lines). Replaced by
`MTG_PREPAY_WILD_DEFER` (default ON): a generic pip gives back the coloured unit it took when a later pip needs that
colour and takes another unit from the float; give-backs are dropped if the float changed since the payment (CR
106.1b safe); the pool always shows real colours. GameState 1496 -> 1504 bytes. Builds clean; NOTHING run yet (3 new
unit tests, suite, repros, tiers, A/B all owed). Temporary `MTG_W_PPDBG` prints in TurnSolver.cpp must be removed.
`build/Release` holds the WIP binary: rebuild on the shared branch before any tier run. Next steps in order: run
units + repros (dragons gi363 may still fail on the 11-vs-7 pricing below); apply the same give-back in the
single-cast payer when a two-mana land (Azorius Chancery) floats its leftover (item 3); remove debug prints; validate;
correct the checkpoint doc's KNOWN GAPS claim; append RESOLVED/PARKED to the Spirits handoff doc. Findings:
- Item 1 dragons overnight d5 s4004 gi363 (seed 4367): the generic drain commits {R} to Urza's Incubator's {3}
  where the legal assignment puts Gruul Turf's {G} there, so Lathliss #2 is refused. Round 2's "KNOWN GAPS
  (permissive, never lossy)" claim is WRONG: committing a generic pip's colour at spend loses legal re-assignments.
  The merge (two units -> one "either colour" unit) fires but the drain spends it before {C}, so it does not yet help.
  Also the 3-spell bill is priced 11 vs 7 available (second Incubator's discount not counted). Alternative to
  consider: defer generic pips' source choice to the end of the bill (exact assignment over the units).
- Item 2 niv gi380: RESOLVED as a CR 106.1b correctness cost (old line paid {R} with {U}); same for niv gi522/gi244.
- Item 3 bruna overnight d5 s7007 gi432 (seed 7439) + niv held-out gi815: `MTG_PAY_HAND_GENERIC` spends Azorius
  Chancery's {U} on Sol Ring's {1} because the hand-demand count cannot see Bruna wished from the sideboard
  (Glittering Wish). Not fixed.
- Item 4 niv held-out seed 671138 gi1138 and edf gi549: legal old lines lost; retest once the merge works.
- USER ruling to honour: floating wild mana "shouldn't show up in anything" -- a merged unit must render as concrete
  taps in the viewer/logs.
- USER: "Draws Diverge is not an acceptable explanation, since at d8 b0 we should still be able to find the line."
- After W lands, the Spirits session rebaselines the overnight tier.

## USER DECISIONS open (surface them; never settle them)
1. **Item E, Gods Loam** (`wip/gods-gy-option-demoted` 847943ebf, lever `MTG_BP_NEW_ONLY_GY_TARGETS`): an old Life
   from the Loam counts as new at a breakpoint when the graveyard gained a land card. 12/13 ENUM-GAP repros fixed;
   held-out gods 5000/arm net +3 (2 slower, both recover at d8 b0; 1 win lost at the shipped budget). Recommended:
   adopt. If adopted: cherry-pick, smoke, regression, PGO, gate gods on 30000-34999.
2. **Culling the Weak victim fallback** (Shadowborn/Martyr area): sacrifice the battlefield Reaper when hand-Apostle-
   first does not pay (same shape as the Ritual rule). Not implemented.
- Answered 2026-11-14: keep tables are regenerated ONLY by the artifact queue when the USER runs it (USER: "Keep
  tables should only be regenerated by the artifact queue when I run that.") -- the Martyr table is not regenerated
  by this chain.

## TODO queue (serial, in this order, after W)
- **C2 r3 Pirates residual** (pirates 12 issues on 79000-83999; next block 84000-88999): hand-demand keep holds a
  creature-only land for a hand creature the line does not cast this turn, stranding Bolt's {R} (s30080205,
  s30079131, s30079307); s30083181 unanalysed; Staunch Crewmate dig casts an undeclared creature (s30070533);
  Fiery Islet pain tie-break vs Dire Fleet Captain (s30077738). `logs/mana-clean-chain/C2-handoff.md`.
- **H** NC re-decide after a draw. USER: "Yes, it might be painful for cost, but non-clairvoyant lookahead of all types
  should re-evaluate after a draw UNLESS we already knew what the card was from previous actions."
- **G+J** Myriad Landscape always fetches 2 (drop `land_search_allow_partial`; USER: "Why would we not search for 2
  lands with Myriad. Anything else is silly and only worthwhile for clairvoyant cases."); Mind Stone sac-draw offered
  iff (extra-turn spells in hand == 1 AND total mana >= 8 counting the Stone) OR (0 extra turns AND mana retained
  after the sac >= 5; test 5 vs 6, report). Re-size the ET oracle cost, then the full ET gate (long run, 2-hourly
  status timer); USER: "we may need to raise the per-decision limit ... I think its best to actually have all decks
  make it through analysis."
- **K** Jumbo: (1) GSZ-fetched untapper before Overgrowth only when the ranked order fails and the swap pays;
  (2) Overgrowth before a one-mana Aura on any land when the ranked order fails and the move pays -- adopt each only
  if A/B net <= 0, no perf cost, fixes lines; (3) amend `JumboOvergrowthAtBreakpoint` (DecisionProviders.cpp):
  breakpoint = outside-only mana O == 3 OR the stacked land's own S == 3, never mixed O+S == 3.
- **L** Hydras. USER: "The doublers out is better, but not if we can't afford to do it that way." -> fetch before the
  last doubler ONLY when the ranked order cannot pay and the moved order can; A/B.
- **N** Fungus cast order: all USER answers in `docs/design/fungus-cast-order-proposal-2026-11-13.md`; first re-check
  site-9 Burst pops on v3 (Q9), diagnose the count menu (Q16).
- **Item 10 PD leftovers**: ActLineHoldMask (s30027719), then PGO re-gate on 32000-36999 (`logs/mana-clean-chain/10-handoff.md`).
- **Dragonstorm re-measure** once the other session's Dragons processes end (needs no USER question).
- **Shadowborn residual** (9 issues on 60000-64999; next block 65000-69999): parked floatseq class (Sire dropped
  after Diabolic Intent; needs a searched Lotus early-crack colour pin), ENUM-GAP s30061713 / s30062801.

## Report items still owed to the USER
- The wild-mana path's correctness costs (Cyclone/Hinata/Niv +1/+1/+3; Niv gi128/gi143 T4->T5) -- now amended by item W.
- The prepay-before-draw topic.

## Working rules this chain followed (all from the USER; see CLAUDE.md)
Shared-branch push at most once per 4 h per session, subagents never push it; references commit-only and
`scripts/check_references_safe.py` before every push; rebase never merge; never `timeout`; waiters on `kill -0 <pid>`
or DONE markers; agents hand off past ~250K context, normally ending 250-350K, never above 400K; never block on a
question (surface it, take the default, keep going); USER-owned decisions are surfaced, never settled.
