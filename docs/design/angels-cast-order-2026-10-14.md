# Angels: the USER's cast order + the narrow Giada second main (2026-10-14)

**Status: WIP -- code written, NOT YET COMPILED, NOT TESTED, NOT MEASURED.** Work stopped for the night
on the coordinator's instruction (the box was needed for a value-leaf generation). See RESUME below.

## What the USER approved (2026-10-14, verbatim)

> "The angel's order + caveats look good. I agree, though, that the second main is potentially wanted
> here. It would be only exactly cases where Giada can attack and we would use her mana where it would
> be considered. I wonder if we can use that to reduce any associated costs. The idea would be for it
> always only to be one spell maximum. We don't need to consider multiple since she can only pay for
> or help pay for one spell."

The draft (order, reasoning, probes): `docs/design/cast-order-drafts-2026-10-14/Angels.md`. Cross-deck
ruling the same day: *"Let's fully order the cards in each list inasmuch as possible. I don't like the
randomness of choosing the order."*

## Part 1 -- the order (`MTG_ANGELS_ORDER`, heurarm `ANGELS_ORDER`, coded default ON)

`AngelsProvider::CastOrderRank` / `CastOrderTierName` / `ArrangeCastOrder` (src/ai/DecisionProviders.cpp,
after `AngelsProvider::CleanupDiscardCandidates`). All main 1.

| slot | card | key (params) |
|---|---|---|
| land | the drop | structural: ApplyPlanDirect / TakeTurn play it before any cast |
| 1 | Sol Ring | `mana_rock`, noncreature |
| 2 | Lightning Greaves | `is_equipment && equip_grants_haste` |
| 3 | Bishop of Wings | creature, `own_creature_enters_lifegain > 0` |
| 4 | Righteous Valkyrie | `own_creature_enters_lifegain_toughness` |
| 5 | Archangel of Thune | `lifegain_each_own_creature_counters > 0`, no recipient filter |
| 6 | Lyra, Archangel of Dawn | same, with `lifegain_counters_subtypes` |
| 7 | Giada, Font of Hope | `other_subtype_enters_counters_subtype` |
| 8 | Youthful Valkyrie | `own_creature_enters_self_counters > 0` |
| 9 | Lyra Dawnbringer | `LordEffect` template |
| 10 | Lightstall Inquisitor | any other creature (sub-key mana value) |
| 11 | Resplendent Angel | `endstep_lifegain_tokens > 0` |
| 12 | duplicate legend whose name is on our battlefield | state; sub-key = its own slot |
| 13 | Serra the Benevolent | `loyalty_abilities` |
| 14 | Swords to Plowshares, Unexpectedly Absent | `Removal` template, sub-key mana value |
| 15 | anything unrecognised (noncreature) | sub-key mana value |

TOTAL: `rank = slot << 16 | sub << 12 | name ordinal`, the ordinal being the name's alphabetical
position over every loaded definition -- different names never compare equal, plan/hand order decides
nothing. `ArrangeCastOrder` adds, in this order:

* a SECOND copy of a legend in the same cast set takes slot 12 (it is a duplicate once the first resolves);
* caveat (b): no converter (Bishop / Righteous / Seraph Sanctuary) on our battlefield or in the cast set
  -> Thune and Lyra AoD re-keyed behind Youthful Valkyrie (Thune before Lyra AoD), ahead of Dawnbringer;
* caveat (a): the plan carries a haste Equip onto a Giada cast in it and an Angel sits between the
  hoist point and her -> Giada is moved to right after Greaves (or after the slot <= 2 casts when Greaves
  is already on the battlefield) IF a walk that credits her unlocked `{W}` (Angel spells only; fires the
  moment she and Greaves are both out, as `ApplyManaUnlockEquips` does) pays strictly more casts. The
  payable-order fallback cannot do this -- `CreditCastOutput` does not credit a hasted dork's mana.

`=0` -> `GenericProvider` tiers and a no-op arrangement (meant to be byte-identical to ff3e205b).

## Part 2 -- the narrow Giada second main (`MTG_ANGELS_GIADA_M2`, heurarm `ANGELS_GIADA_M2`, coded default OFF)

Generic hooks (`DecisionProvider::RestrictedSecondMainLive` / `RestrictedSecondMainAdmits`, read via
`ProviderSecondMainLive` in DecisionProviders.h, false whenever `uses_second_main`), wired beside Jorn's
`UntapSecondMainLive` at every post-combat site: AIEngine::TakeTurn's two `play_this_phase` gates; in
TurnSolver the rollout's per-turn m2, FSLineTail, the edge-tail elision, the honest per-k evaluation, and
the four SolveWithLookahead candidate-loop `ApplySecondMainInSearch` sites (the decisive "cast now vs
hold for after combat" sites -- the Omnitool predicate is NOT wired there; Giada's must be, or main 1
could never choose to hold Lightstall). The menu filter sits at the tail of `CollectActions` beside the
Omnitool's (post-combat, not human play).

AngelsProvider's rule:
* OPENS when our untapped vigilant subtype-restricted mana dork (Giada) passes `CanAttackFull` and
  `CanTapNow` post-combat, and some Angel spell in hand is payable with `AvailableManaPool(s)` but not
  with `AvailableManaPool(s, giada)`.
* MENU = exactly those Angel casts. No other cast, no land (held lands are barred from m2 already), no
  equip, no activation. At most one cast is ever payable (each needs her one `{W}`).
* SOUNDNESS: an Angel payable without her was payable in main 1 together with main 1's casts without
  tapping her (the payer holds an attack-capable dork back), and is weakly better there in a goldfish --
  so the restriction is an identity fold, nothing unfindable at unlimited budget. Goldfish-specific.

**USER QUESTION (interpretation, conservative default taken):** I read "would pay for, or help pay for"
plus "it must use Giada's mana" as: the m2 admits only an Angel that is NOT payable without her. An
Angel payable from the lands alone would also be castable in main 1 (and better there), so admitting it
would only add a frame. If you meant "any Angel her mana could contribute to", say so -- it is a
one-line change in `RestrictedSecondMainAdmits` / `RestrictedSecondMainLive`.

## Tests written (not yet run)

* `test/unit/test_angels_cast_order.cpp` (registered in CMakeLists.txt): the order; duplicate legends;
  40 hand permutations -> one realised order (`CanonicalNonSacCastOrder`); caveat (b) with/without each
  converter; in-set duplicate Giada; caveat (a) on 6 vs 7 Plains incl. `ApplyPlan` realising all three
  casts, and the draft's 4-d board; `=0` == generic; Giada m2 open/closed cases (sick, tapped, Bishop,
  payable-without, Greaves-hasted); the menu offers <= 1 cast; vigilance + post-combat tap end to end.
* `test/scenarios/angels_order_bishop_before_lightstall.json` (draft probe A, life 24),
  `angels_order_converter_before_engine.json` (probe B2, life 24),
  `angels_giada_m2_attack_then_pay.json` (probe 4-e with the lever: win T5),
  `angels_giada_m2_off_no_t5.json` (control, lever off: no win by T5).

## Findings so far

* The draft's probes reproduce on the integration tip binary (ff3e205b, `bin/mtg.base`), byte-for-byte
  with the draft: A 20 vs 24 life by hand order; B2 Thune-first no counters; D 20 vs 21; G 12 vs 10 opp
  life; L 3 vs 1; H1/H2 Greaves stranded; M (4-e) no T5 win in main 1 only, T5 win with
  `MTG_FORCE_USES_M2=1`; V1 accept / V2 illegal. Logs: `build/scratch/angels-order/probes/base/`.
* Vigilance itself is modelled (Combat.cpp's attack-tap honours `CreatureHasVigilance`); the card
  note's "attack and still tap for Angel mana the same turn" is false only because Angels plays no
  second main. Part 2 makes it true when the lever is on; the note should be amended when it is adopted.

## RESUME (for the next session)

**Base:** worktree branch `worktree-agent-af1d7142114addb01`, reset to ff3e205b (work/integ-2026-10-14),
WIP commit on top. Base binary: `build/scratch/angels-order/bin/mtg.base` (+ `mtg-test.base`,
provenance `mtg.base.meta`: HEAD ff3e205b, HEAD:src 2a228890).

**Done:** the code above, the tests above, two heurarm slots appended (ANGELS_ORDER, ANGELS_GIADA_M2),
manifests generated.

**Not done, in order:**
1. Build: `bash /workspaces/MagicDeckTester/build/scratch/build2.sh` from the worktree root (the first
   attempt was stopped at 57/112, before any src/ai object compiled -- expect compile errors to fix).
   Copy `build/Release/mtg` to `build/scratch/angels-order/bin/mtg.new`.
2. `build/Release/mtg-test` (the new test file + everything else) and `bash test/scenarios.sh`.
3. Probes old vs new: `python3 logs/angels-order/run_probes.py new <bin/mtg.new>` and with
   `MTG_ANGELS_GIADA_M2=1` (the base run is in `probes/base/`).
4. The pooled A/B. Manifests (regenerate with `python3 logs/angels-order/make_manifests.py`):
   `build/scratch/angels-order/ab/base.json` (177 jobs, 49,104 games: angels / angels2hg OLD arm + every
   other suite deck's identity cells) and `ab/new.json` (189 jobs, 101,004 games: arms P1 = order only,
   P12 = order + Giada m2, OFF = both levers off on a seed prefix, + identity at new defaults). Seeds:
   angels ship 11400001 (4000 g, budget 20, depth from value_play = 5), d3 b10 11410001 (6000), d0
   11420001 (20000); angels2hg ship 11450001 (2000), d3 11460001 (3000), d0 11470001 (10000); identity
   24 games at 11500001 / 11510001 / 11520001. Run both binaries concurrently, cwd = the worktree:
   `bash logs/angels-order/run_batch.sh base <bin/mtg.base> ab/base.json <threads>` and
   `bash logs/angels-order/run_batch.sh new <bin/mtg.new> ab/new.json <threads>`.
   The base batch was started tonight and killed after ~4 min with NO job finished -- nothing measured.
5. `python3 logs/angels-order/analyze.py --slower-json ab/slower.json`: per cell faster / slower / net
   (loss = 9), play-changed, units ratio; the OFF arm must be byte-identical to the base; identity jobs
   must not differ.
6. Every slower game: stage 1 `--depth W --budget-ms 100` (W = max(5, old win turn)), stage 2
   `--depth 8 --budget-ms 0`, both as ONE pooled manifest of single-game jobs
   (`seed = base + gi, games 1, game_index gi`, the arm's flags). Root-cause any that do not recover.
7. References: `test/viewer_protocol_check.py --strict` and `scripts/ref_bench.py` for Angels, old vs new.
8. Defaults: part 1 ON if it meets the bar; part 2 ON only if it meets the bar AND its units ratio is
   small, else OFF with the numbers for the USER. Then the Angels section in cast-order-rankings.md
   (`mtg decks/Angels/Angels.cod --profile decks/Angels/Angels.profile.json --cast-order-report`).

Scratch scripts (gitignored, on the persistent host mount): `logs/angels-order/{run_probes.py,
make_manifests.py, run_batch.sh, analyze.py}`. Scratch outputs: `build/scratch/angels-order/`.
