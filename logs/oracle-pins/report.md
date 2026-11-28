# Oracle pins: Fireblast / Walk the Aeons pinned sacrifices as plan sources (gate claude-queue-20261127a, triage O1 + O5)

Branch `wip/oracle-pins` (from origin/phase-1-2-deck-analyzer a21ea18e9). Fix commit 7cc3ee438. Oracle only: play is unchanged (shown below).

## Mechanism (confirmed on repros)
Fireblast's alternative-cost menu entry pins its two Mountains in `tutor_target` as `fireblast:<n1>,<n2>` (TurnSolver.cpp ~46522).
`Realize` built the plan's own sources (`plan_srcs`, which `KeyedApart` keys apart from twins) only from `sac_source_id`,
`sac_victim_id` and `enchant_target`, so the pins never reached it. A land's age is folded (`AgeIsInert`), so the pinned
old Mountains and this turn's new Mountain were one resource in the frontier prune.

1 Drop Burn s30000228 T3, `{land=Mountain + Fireblast + Lava Spike + gy-exile:Grim Lavamancer}`, board Mountain #31, #32 (pinned),
new #33. The engine pays Lavamancer's {R} with #31 and Lava Spike with #32, then sacrifices both, and #33 stays up. The level-3 table at
baseline: frontier pay1 lists `Mountain#33:R` and `Mountain#31:R`, but only the `pay1[Mountain#33:R]` frontier goes on to pay2. The
`pay1[Mountain#31:R]` frontier was pruned as its twin, so every oracle outcome has #33 tapped plus a stranded {R}: "Mountain#33 tapped
by oracle only; float engine - oracle {R}", ORACLE-GAP.

Walk the Aeons' `buyback:<n1>,<n2>,<n3>` pin had the same gap (Extra Turns is the only deck with it; it is not in this gate, so
there is no measurement for it).

## Fix (src/ai/ManaOracle.cpp)
- `InsertCostPinSources(action, set)` parses a CastFromHand's `fireblast:` / `buyback:` pin into card numbers. `Realize` adds them to
  `plan_srcs`, as it does `sac_victim_id`. The FoldStats diagnostic's matching source set gets them too.
- O5: `KindText` names `CompanionToHand` "companion", so the Kaheera plans no longer print `kind?:`. It was the only kind missing from the switch.
- Unit test `mana oracle: Fireblast's pinned Mountains pay before they are sacrificed, the new Mountain stays up`
  (test/unit/test_mana_oracle.cpp). It checks the premise (the engine sacrifices #1/#2 and keeps the new #100 up), then that
  `CoversEngineOutcome` holds and the verdict has no ORACLE-GAP. Against a build with the one `InsertCostPinSources(a, plan_srcs)`
  call removed, exactly the two oracle assertions fail ("verdict: ORACLE-GAP"), so the test catches the defect.
  Full unit run (fix): 4348 / 4348 cases PASS (unit_par, 2 cores).

## Before / after
Single-game repros, `MTG_MANA_ORACLE=3`:

| repro | base ORACLE-GAP | fix ORACLE-GAP | fix classes |
|---|---|---|---|
| 1 Drop Burn s30000228 T3 | 10 | 0 | none |
| 1 Drop Burn s30000473 T3 | 11 | 0 | none |
| Creature Burn s30001127 T4 | 1 | 0 | none |

Sample: 400 one-game jobs, `--batch --threads 2`, `MTG_MANA_ORACLE=2` (as mana_clean). For each deck: games 0-49 plus 150 games
drawn (seeded) from those with a whole-line ORACLE-GAP in the gate. Manifest: build/scratch/oracle-pins/sample.json.

| deck | class | base | fix |
|---|---|---|---|
| 1_drop_burn | ORACLE-GAP | 564 | 0 |
| 1_drop_burn | PHANTOM | 1596 | 1596 |
| creature_burn | ORACLE-GAP | 179 | 0 |
| creature_burn | DOMINATED-LAND | 41 | 44 |

Every base ORACLE-GAP in the sample was a Fireblast plan (743 of 743), and all of them clear. No MISS / ISSUE class grew.
DOMINATED-LAND (context, not an issue class) gained 3, all in games 35 and 3625 of Creature Burn: with the pinned Mountains keyed apart, the
oracle now reaches "tap pinned Mountain #31 (and #32) for {R} before Fireblast eats them" and sees that it keeps Teetering Peaks
up where the engine tapped Peaks. That payment existed before; the old fold hid it. It is the oracle becoming more complete, the same
change that removes the gaps.

Oracle cost: the sample's oracle-on job time rose from 56.1 s to 65.3 s (+16%) because the pinned copies are no longer folded. Play cost
(oracle off) is unchanged: 40.0 s and 39.6 s.

## Play identity
Same 400 games, per-job `digest=`:
- oracle off (L0): base vs fix, 0 / 400 differ.
- oracle on (L2): base vs fix, 0 / 400 differ; fix L2 vs base L0, 0 / 400 differ.

The release `mtg` rebuilt from the committed tree is byte-identical (cmp) to the measured fix binary.

Scratch: /workspaces/MagicDeckTester/build/scratch/oracle-pins/ (sample.{base,fix}.L{0,2}.{out,err}, r*.{base,fix}.err, units/).
