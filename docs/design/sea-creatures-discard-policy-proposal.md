# Sea Creatures — cleanup-discard BUCKET policy (PROPOSAL, 2026-11-17)

**Status: PROPOSAL, NOT IMPLEMENTED.** Discard buckets are USER-owned. Nothing ships until the USER rules.
On approval it lands as `SeaCreaturesProvider::CleanupDiscardCandidates` behind
`EnvOn("MTG_SEA_BUCKET_DISCARD", true)` (+ a HeuristicArm slot), returning the full shed order (most expendable
first, every non-staged hand card named) through `CleanupDiscardRankingWithOrder`; `=0` restores
`GenericProvider::CleanupDiscardCandidates` (max-MV). Same contract as the Giants / Bruna / PD ladders
(`giants-discard-policy-proposal.md`).

## Why it matters (and how little)

* Today the deck runs the GENERIC max-MV shed: it throws the BIGGEST card first. In this list that is an 8-drop
  payoff (Trench Gorger / Lorthos / Brinelin) ahead of a never-cast Whelming Wave (MV 4) -- backwards for a
  ramp deck whose Wave is dead by the USER's ruling (`MTG_SEA_WAVE_NEVER`, ledger Q15a).
* Frequency: 6 cleanup decisions in 400 games at d5 b20 (Stage 5i, `MTG_SHED_STATS`), all 6 STATUS_QUO_OK
  (0 regret). So the ladder is about correctness of the rare case, not a measurable win-turn mover.
* The best-vs-worst shed bound (`MTG_SHED_WORST` / `MTG_NONCLEANUP_SHED_WORST` arms, d3 b10 and d5 b20,
  `build/scratch/sea/discbound/`) is re-running after the 2026-11-17 mount loss; its number goes into the ledger.

## Proposed buckets (agent draft; every line is the USER's to overturn)

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

## Questions for the USER

1. Wave first: agreed it is always the first shed while `MTG_SEA_WAVE_NEVER` is on?
2. Mana target 7 + soft 8th, and the within-mana order (especially Overgrowth vs Powerstone vs Sprawl)?
3. Threat order: Koma > Kiora > Spawning Kraken > Hullbreaker > Leviathan > Serpent > Lorthos > Brinelin >
   Gorger > Sin -- and whether Spawning Kraken's "with another sea creature" promotion is wanted.
4. Threat quota 2 + soft 3rd (the Giants shape)?
