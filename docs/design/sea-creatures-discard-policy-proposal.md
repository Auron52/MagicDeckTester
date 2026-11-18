# Sea Creatures — cleanup-discard BUCKET policy

**Status: USER RULED 2026-11-18 — BUILT, default ON** as `SeaCreaturesProvider::CleanupDiscardCandidates` behind
`EnvOn("MTG_SEA_BUCKET_DISCARD", true)` (heurarm slot `SEA_BUCKET_DISCARD`). It returns the full shed order (most
expendable first, every non-staged hand card named) through `CleanupDiscardRankingWithOrder`; `=0` restores
`GenericProvider::CleanupDiscardCandidates` (max-MV). Same contract as the Giants / Bruna / PD ladders
(`giants-discard-policy-proposal.md`). Unit tests: `test/unit/test_sea_discard.cpp`. The agent draft below the ruling
("Agent draft, superseded") is kept as background only; wherever it differs, the ruling wins.

## The USER's ruling (2026-11-18, verbatim, in the order given)

1. *"We need a proper bucketing with two main buckets of mana and threats and beyond that want some level of
   different mana costs or acceleration and lands. I think that Spawning Kraken 1-of should be an auto-keep in
   threats and same with Koma since those snowball. The others should be kept based on P/T and castability. We
   should keep a mix of mana costs. For mana you are looking for 6-8 mana in hand or on board, (mostly acceleration
   and 1 or 2 lands) but need to keep at least 2, but preferably 3 threats."*
2. *"I guess at the very beginning 5 mana and 2 threats would be correct, but that can shift to 4 and 3 when we have
   more mana out."*
3. *"Depending on what the threats are, we may need to keep 7 mana before we start making more than 2 spots for
   theats. Not being able to play them is a real headache."*
4. *"And usually we should aim for a 6 and a 7 drop to start, but this isn't required if we have more space or are
   already nearing 8 mana."*
5. *"I don't think Sin is as bad as you made it out to be. If you play it with Kiora bests the sea god or even
   Summon: Leviathan in particular it is high value, but it is conditional."*
6. *"If we have a curve and Sin fits well into that curve it might be a keep. Summon: Leviathan -> Sin is not
   crazily powerful, but it will finish the opponent on the turn Sin attacks."* — qualifier: *"(at 20 life
   anyway)"*
7. *"In terms of mana, you typically want a mix of cheap acceleration and 1 3 mana acceleration spell if it is early
   in the game to ensure an early 6-drop."*
8. *"And beyond that a land drop for each turn."* — qualifier: *"(which probably means 1 or 2."*
9. *"I guess on 20-life it is possible that sin + Summon Leviathan is actually better than Summon Leviathan + Koma.
   If we have Kraken, on the other hand, the game should end fast enough with any 7-drop."*
10. *"It's more like we should keep Koma or the biggest 7-drop if we have Spawning Kraken."* (corrects the parent's
    first reading of 9's Kraken half, "pick the 7-drop by castability"; castability is NOT the criterion there)

## What was built

Classification is by params / types (never card names), so a screening arm that swaps a card keeps its bucket.

**Buckets.**
* BLANK — Whelming Wave (`bounce_all_creatures`; never cast under `MTG_SEA_WAVE_NEVER`): always the first shed.
* DEAD — a legendary card with a copy on our battlefield or an earlier copy in hand (a 2nd Koma; also Lorthos /
  Brinelin / Sin, by the same legend rule).
* HOSTLESS — a land Aura with no legal host: no land of `land_aura_host_subtype` (Utopia Sprawl: Forest; Wild Growth
  / Overgrowth: any land) on our battlefield or in hand, and no held / fielded fetch whose search can find one in the
  library. Counts 0 mana; shed after DEAD.
* MANA — lands 1, rocks `produces_amount` (Sol Ring 2, Worn Powerstone 2), hosted land Auras `land_aura_extra_mana`
  (Overgrowth 2, Utopia Sprawl 1, Wild Growth 1), counted together with the battlefield's production.
* THREATS — creatures and token Sagas. Value = P + T (Kiora: its 8/8 token; Trench Gorger: L/L for L >= 6 library
  lands, the USER's take-all rule, else 6/6; Sin below) times castability, the Giants / Minotaur table
  {1, .70, .35, .10, .03, .02} by mana short of the KEPT reach (battlefield + kept mana cards).

**Threat precedence (highest first).**
1. Auto-keeps: the first Spawning Kraken and the first live Koma (rule 1). Extra copies rank as ordinary threats.
   * **With a Spawning Kraken in hand (rules 9, 10):** T1 = Kraken, T2 = Koma when held, otherwise the BIGGEST MV-7
     threat by the same conditional value used everywhere (Sin with its harvest, Kiora's token, P + T). Castability
     is not consulted (every candidate is MV 7) and no Sin / Summon promotion applies to that slot. No MV-7 threat in
     hand: step 3 fills T2.
   * **Without a Kraken (rule 9):** a CREATURE-Saga line -- Summon: Leviathan held, or on the battlefield with lore
     -- whose Sin finisher projection (step 2) reaches the opponent's actual life takes the slots AHEAD of Koma:
     [held Summon,] Sin, then Koma. With two slots Summon + Sin beats Summon + Koma at 20 life; Koma keeps the 3rd
     slot whenever there is one; at 2HG's 30 the projection (21) falls short and Koma stays T1.
2. Sin as the FINISHER (rule 6 + "(at 20 life anyway)"): promoted when its Saga line's projected damage through the
   turn Sin attacks reaches the opponent's CURRENT life (`players[1].life`, so 2HG's 30 is read as 30), with Sin
   castable off the kept reach. A held Saga of that line is promoted with it. Projection (cumulative, the line the
   USER described): a Saga on the battlefield at lore L -> next turn it reaches L + 1, our creatures attack, Sin is
   cast in main 2 harvesting L + 1; the turn after Sin attacks beside them: `2 x board power + Sin's power + mult x
   (L + 1)`. A held Saga -> cast next turn (summoning-sick), Sin the turn after at lore 2, Sin attacks the turn after
   that: `3 x board power + 2 x the Saga's first body + Sin's power + mult x 2`. Summon: Leviathan -> Sin = 6 + 15 =
   21: a kill at 20, not at 30 -- matching "(at 20 life anyway)". (The first build projected Sin's attack turn only,
   15, so the line could never finish at 20; fixed with rule 9.)
3. The opening pair (rule 4): while fewer than two threats are fixed, a 6-drop and a 7-drop (MV <= 6 and MV 7) when
   the hand has both, the best such pair by summed value x castability. Relaxed to the best pair on merit once the
   kept reach (board + the first 5 mana cards) is >= 7 ("nearing 8"). Sin competes for the 7 slot with its
   conditional value, scored WITH its partner (so Summon: Leviathan + Sin is valued as the line).
4. The rest on merit, one at a time. Mix of costs (rule 1): a candidate whose MV is not yet covered by the kept
   threats beats a covered one when it is within 20% of it (for the opening pair: a distinct-MV pair beats a
   same-MV pair within 20% of its sum).

**Sin's value (rules 5, 6).** 5/5 = 10, plus, for each of our Sagas still on the battlefield at our NEXT main 2
(lore + 1 < chapters: Sin is a main-2 cast under the Kraken-only main-1 rule, and a Saga reaching its last chapter
is sacrificed in main 1), the strip-every-Saga harvest (`MTG_SEA_SIN_STRIP_SAGAS`): +2 x 2 x (lore + 1) as P/T,
PLUS the re-fired chapter I (Kiora: another 8/8 Kraken = 16; Summon: Leviathan: its 6/6 body is not sacrificed =
12). With no such Saga, a Saga kept in hand (cast next turn, Sin the turn after at lore 2) adds the same at lore 2
weighted by the Saga's own castability. No Saga line: a plain 5/5.

**The 5/2 -> 4/3 split (rules 2, 3).** `need` = max(6, highest MV among the two fixed threats). EARLY while
battlefield + the first 4 mana cards of the keep order < `need`: 5 mana cards + 2 threats (the 3rd threat sheds
before the 5th mana card). LATER: mana up to `need` (at most 4 cards), then the 3rd threat, then mana up to 8 (the
5th mana card sheds before the 3rd threat). With a kept 7-drop the 3rd slot opens only at 7 mana; with an 8-drop, 8.

**The mana keep order (rules 1, 7, 8).** While the battlefield alone makes < 6 mana ("early in the game"):
land, cheap accelerant (MV <= 2), cheap accelerant, ONE MV-3 accelerant (Overgrowth before Worn Powerstone:
production, then enters untapped), [2nd land], the other cheap accelerants, the other MV-3 accelerants, surplus
lands. From 6 battlefield mana: land, accelerants by production, [2nd land after the first three], the rest,
surplus lands. Lands kept = the land drops needed for battlefield + the planned accelerants (those before the
2nd-land slot) to reach `need`, clamped to [1, 2]; lands past that are surplus and shed before every accelerant.

**Shed order.** Wave > dead legend copy > hostless land Aura > mana past 8 (the keep order's tail first) > the 4th+
threats (worst first) > unrecognised cards > mana between the floor and 8 > the 3rd threat > the floor mana
(backwards) > T2 > T1. `CleanupDiscardShedStable()` is false while on (re-ranked per shed, the Giants precedent).

**Agent readings (each the USER's to overturn).**
* "Early in the game" for the mana mix = battlefield production < 6 (board-only, so it cannot loop through the
  5/2 split it feeds). The parent's first suggestion (the 5/2 state itself) is circular: the state is computed off
  the keep order the mix decides.
* "Nearing 8" = kept reach >= 7 (board + the first 5 mana cards of the provisional order).
* The opening pair's castability reads a provisional mana order built for `need` = 6; `need` then fixes the land
  count and the final order, and the state is read off the FINAL order.
* With a third slot open, the opening pair stays as chosen and the 3rd slot is free on merit (rule 4's "isn't
  required if we have more space" read as "the 3rd slot is not bound to the pair").
* Castability is the simple deficit table (the parent's reading), not the shared `discard_dist` model.
* Sin's hand-Saga line uses lore 2 (Saga cast next turn, Sin the turn after) and no extra discount beyond the
  Saga's castability; the finisher test is cumulative damage through the turn Sin attacks against the CURRENT life.
* Rule 9's promotion ahead of Koma is read for the CREATURE-Saga line (Summon: Leviathan) only, as the USER named
  it; a Kiora line that projects a kill keeps the step-2 place (behind Koma). Plausible reason the USER singled out
  Summon: it is a 6-drop, so Summon + Sin fills the 6 + 7 curve, while Kiora + Sin is two 7s.
* Rule 10's "7-drop" = MV exactly 7 (Koma, Hullbreaker, Kiora, Sin); the MV-8 threats (Gorger, Lorthos, Brinelin)
  compete only from T3 on. "Kept" Kraken = held in hand; a Kraken already on the battlefield does not trigger (a1).
* Kraken is T1 and its 7-drop T2 (the first build had Koma T1, Kraken T2); the order only matters when shedding
  down to a single threat.
* Precedence when rules meet: Kraken + (Koma | biggest 7) > [Summon +] Sin finisher on a creature-Saga line > Koma
  > Sin finisher on any other line (+ its held Saga) > 6/7 pair > merit.
* Census tracing: `MTG_TRACE=discard` prints one `sea ...` line per REAL cleanup decision (state, mix, need,
  reach4/5, lands_raw = the unclamped land count, lands_keep, the full shed order). Diagnostic only.

## Measurements

Build d3b93f4f6 (rules 1-10), d5 b20.

* Units: 16 Sea discard cases pass; full suite 3489/3489.
* Scoped suite (`regression.sh --both --deck=sea,sea2hg`, NOT accepted): only Sea rows move. Every aggregate is
  unchanged except sea2hg regression d5 s2002 6.3867 -> 6.3733 (gi61 T8 -> T7). Smoke searched slower 0 / faster 0
  / play-changed 2; regression searched slower 0 / faster 1 / play-changed 12; d0 slower 0. No slower game, so no
  root-cause owed. GT still to accept (parent).
* Held-out paired A/B, ONE pooled batch (ON vs `MTG_SEA_BUCKET_DISCARD=0`; std 1000 seeds 9600001, 2HG 300 seeds
  9700001; 24/24 workers): **std 5.9090 vs 5.9090, delta +0.0000, se 0, faster 0 / slower 0, units 0.999; 2HG 6.3100
  vs 6.3100, delta +0.0000, faster 0 / slower 0, units 0.998.** Outcome-identical in all 1300 games.
* Census (`MTG_TRACE=discard`, real cleanups only): **21 decisions per arm in 1300 games** (the same 21 (seed, turn)
  cleanups in both arms), 16 of them with a different first shed. State: early 5/2 = 12, later 4/3 = 9; the
  early-game mana order (board < 6) applied in all 21. The unclamped land count would be 3+ in 15 of 21 (1: 2, 2: 4,
  3: 9, 4: 5, 5: 1), but 18 of the 21 hands held NO land (kept lands 0) and 3 held one, so the [1, 2] clamp never
  bound. ON first sheds: Whelming Wave 7, Brinelin 4, a dead 2nd Koma 3, hostless Utopia Sprawl 2, Serpent 2, Sin 1,
  Kiora 1, Hullbreaker 1. `=0` first sheds: Brinelin 8, Gorger 2, Sin 2, Serpent 2, Lorthos 2, Koma 2, Summon: Leviathan 2, Hullbreaker 1. These cleanups come from mana-screwed hands (18 of 21 with no land in hand), which is why the choice never moves a win turn: the rule is correctness for the rare case, as
  the Stage 5i bound (~0 turns) predicted. Files: `logs/sea-discard/census-result.txt`, `logs/sea-discard/reg.log`.

## Agent draft (2026-11-17), superseded by the ruling above

### Why it matters (and how little)

* Today the deck runs the GENERIC max-MV shed: it throws the BIGGEST card first. In this list that is an 8-drop
  payoff (Trench Gorger / Lorthos / Brinelin) ahead of a never-cast Whelming Wave (MV 4) -- backwards for a
  ramp deck whose Wave is dead by the USER's ruling (`MTG_SEA_WAVE_NEVER`, ledger Q15a).
* Frequency: 6 cleanup decisions in 400 games at d5 b20 (Stage 5i, `MTG_SHED_STATS`), all 6 STATUS_QUO_OK
  (0 regret). So the ladder is about correctness of the rare case, not a measurable win-turn mover.
* The best-vs-worst shed bound (`MTG_SHED_WORST` / `MTG_NONCLEANUP_SHED_WORST` arms, d3 b10 and d5 b20,
  `build/scratch/sea/discbound/`) is re-running after the 2026-11-17 mount loss; its number goes into the ledger.

### Proposed buckets (agent draft; every line is the USER's to overturn)

**MANA** -- count sources on the battlefield plus castable ones in hand. Threats cost 6 (Leviathan, Serpent,
Spawning Kraken), 7 (Koma, Hullbreaker, Kiora, Sin) and 8 (Lorthos, Brinelin, Gorger), so the target is
**7 mana of production, soft 8th** when there is room. Lands come first while fewer than 4 are in play (land
drops are the deck's floor and every land Aura needs a host). Within mana:
Sol Ring > land (while < 5 lands in play) > Overgrowth (+GG) > Worn Powerstone (+CC, enters tapped) >
Utopia Sprawl (+U, the USER's blue rule) > Wild Growth (+G, green is the minor colour) > surplus lands.
A land Aura with no legal host anywhere (no land on board or in hand; Sprawl with no Forest-typed land:
Breeding Pool, Forest, Hedge Maze) counts as a blank.

**THREATS** -- 2 + a soft 3rd. Order (best goldfish clock first, discounted by distance to castable, the
Minotaur/Giants EV = P(play) x value rule):
Koma, World-Eater (8/12 trample, four 3/3 Serpents per hit) > Kiora Bests the Sea God (8/8 hexproof at once) >
Spawning Kraken (a 9/9 per sea-creature hit; promoted with another sea creature in play) > Hullbreaker Horror
(7/8) > Summon: Leviathan (6/6, chapters II/III draw off sea-creature attacks) > Serpent of Yawning Depths
(6/6; its evasion does nothing against a non-blocking goldfish) > Lorthos (8/8 at 8) > Brinelin (6/8 at 8) >
Trench Gorger (8 mana, size = lands left in library) > Sin (5/5 flier at 7; grows only off Saga counters).

**KEEP ladder:** land, threat, mana, land, threat, mana, mana, soft 3rd threat, soft 8th source.

**SHED order:** Whelming Wave (never cast) -> a 2nd copy of a legendary with one in play (Koma, Brinelin) ->
hostless land Aura -> mana past the soft 8th (Wild Growth, then basics, then Powerstone) -> 4th+ threat
(lowest discounted value first) -> the ladder tail read backwards.

### Questions for the USER (answered by the ruling above)

1. Wave first: agreed it is always the first shed while `MTG_SEA_WAVE_NEVER` is on?
2. Mana target 7 + soft 8th, and the within-mana order (especially Overgrowth vs Powerstone vs Sprawl)?
3. Threat order: Koma > Kiora > Spawning Kraken > Hullbreaker > Leviathan > Serpent > Lorthos > Brinelin >
   Gorger > Sin -- and whether Spawning Kraken's "with another sea creature" promotion is wanted.
4. Threat quota 2 + soft 3rd (the Giants shape)?
