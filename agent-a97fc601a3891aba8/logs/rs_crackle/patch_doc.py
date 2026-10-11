#!/usr/bin/env python3
"""Replace the design doc's follow-ups section with the two implemented hold-rule levers."""
p = '/workspaces/MagicDeckTester/.claude/worktrees/agent-a97fc601a3891aba8/docs/design/reality-spasm-crackle-classification.md'
s = open(p).read()
start = s.index("## Follow-ups (deferred, not implemented; each would need its own paired A/B)")
new = '''## The hold rule's two follow-ups (implemented on top of the fix)

Both levers live in `HinataProvider` and share one helper, `HinataLoneCrackleHeld`. That helper is the
whole hold rule (enabled, Crackle, autonomous, fewer than two copies, and the B scope gate) **except**
the "wins THIS turn" exemption. So the arms differ only in how lethality is judged and in scope.

### A. `MTG_HINATA_HOLD_LETHAL_BURN` (heurarm `HINATA_HOLD_LETHAL_BURN`) — correctness, DEFAULT ON

The hold's lethal test was `max_x * 5 * face_hits + attack_ub >= opp_life`, an estimate that counts
the Crackle and every creature's power and nothing else. With the lever on, `XCandidates` offers the X,
and a new provider hook, `DecisionProvider::HoldCastUnlessLethal`, flags the lone Crackle. Both subset
walks (`Solve`'s consider and `EnumeratePlans`' consider, the only two consumers of `CollectActions`)
then drop every subset that casts a flagged Crackle and does not win by **their own** projection.
That projection is `projected_atk + direct_dmg >= opp life`, on a subset that already passed
payability. This is the `HoldSacLandBurn` / Irencrag-waste shape: no second estimator; the engine's
damage and payability machinery decides. A co-cast Magma Opus or Soulfire now counts. So do summoning
sickness and creatures tapped for mana, which the estimate over-credited. The base hook is `false`, so
every other deck is byte-identical (verified on burn, dragonstorm and stompy).

* Class-3 games recover: d0 gi 157 and 196 go from 7 to 6, replaying the pre-fix exact-20 lines (RS RS
  Crackle X=3 + Magma Opus; RS + Magma + Crackle X=3).
* Unit test: a board where Crackle X=4 + attack = 24 < 25 but Crackle X=4 + Magma Opus (1 face) +
  attack = 25. With the lever on, Solve and the search both cast the pair; with it off, nothing casts
  the Crackle. At life 26, neither arm casts it.

### B. `MTG_HINATA_HOLD_NEEDS_HINATA` (heurarm `HINATA_HOLD_NEEDS_HINATA`) — scope, DEFAULT OFF (USER decides)

Hold only while the combo is reachable: Hinata in play or castable from hand this turn
(`HinataCastableFromHand`, the same optimistic source-count gate as the same-subset credit).

* Class-1 games under B: d0 gi 8 (9→8), 212 (9→6), 324 (8→7) and shipped gi 36 (9→8) recover.
  2HG gi 90 does not (9): Hinata sits in hand, castable by count, so the hold stays on. The d0 runner
  then never casts her (it prefers a chip Crackle on T5, and Gamble bins her on T6).
* **Held-out, B measures WORSE** (table below): the hold-without-Hinata is worth having on average.
  Recommendation: keep it OFF.

### Held-out measurement (one pooled batch, seeds 8100001+, three arms, paired by gi, unwon = 9)

Arms: `base` = shipped defaults (A on, B off); `aoff` = A off, which is the 2cec6ec4 behaviour;
`bon` = B on. Lever A = base vs aoff; lever B = bon vs base.

| lever | cell | n | Δ turns (on − off) | Δ/game | t | faster | slower | search units on/off |
|---|---|---|---|---|---|---|---|---|
| A | shipped b20 | 400 | 0 | 0 | — | 0 | 0 | 1.0058 |
| A | d3 b10 | 400 | +1 | +0.0025 | 1.00 | 0 | 1 | 1.0007 |
| A | 2HG d0 | 400 | 0 | 0 | — | 0 | 0 | n/a |
| A | d0 | 3000 | −2 | −0.0007 | −1.00 | 3 | 1 | n/a |
| B | shipped b20 | 400 | +3 | +0.0075 | 0.56 | 8 | 10 | 1.038 |
| B | d3 b10 | 400 | −5 | −0.0125 | −2.25 | 5 | 0 | 1.051 |
| B | 2HG d0 | 400 | +32 | +0.0800 | 3.63 | 2 | 20 | n/a |
| B | d0 | 3000 | +161 | +0.0537 | 8.75 | 15 | 117 | n/a |

Lever A is net −1 turn over 4200 games for +0.6% / +0.07% search units, a correctness change that
rarely binds. Byte-identity: with both levers `=0` (env path) against the 2cec6ec4 binary, 36 jobs and
1600 Hinata games (d0 1000, 2HG 400, d3 100, shipped 100) gave 0 digest mismatches.

**Lever A's two slower games:**

* d0 gi 1683 (6 → 7). On T5 the off arm's estimate took max X *with* the ritual credit, and every
  creature's power, and called Crackle "lethal". The real line was RS + Crackle X=3 = 15 + 4 = 19,
  leaving the opponent at 1, and Soulfire finished on T6. The walks' projection correctly calls that
  subset non-lethal, so the hold applies. The game wins T7 with RS RS Crackle X=10. The chip clock was
  faster here; that is the hold's own trade-off, no longer masked by an over-optimistic estimate.
* d3 gi 25 (7 → 8). The first divergence is T2: Ponder now (off) vs hold the cantrips to T3 (on). It is
  a near-tie, scored by rollouts whose futures now hold a non-lethal lone Crackle exactly. After that,
  the two arms draw different cards.

**Lever B's 147 slower games** (every one reproduces single-game; per-game list in the worktree's
`logs/rs_crackle/classifyB.out`, made by `classifyB.py`):

* **78 (B1):** with Hinata neither in play nor castable, B lets the d0 runner spend the lone Crackle as
  a chip (X = 1–2). The base arm holds it and later fires that same Crackle **with Hinata in play** as
  the combo finisher. (d0 68, 2HG 10.)
* **54 (B2):** the chip is spent; the base arm fires the held Crackle later, still without Hinata, at a
  bigger X (3–5) or beside a second copy. Holding converts an X=1 chip into an X=3–5 payload.
  (d0 45, 2HG 9.)
* **1 (B3):** d0 gi 2827. The chip is spent and the base arm never needs the card.
* **10 (B4, shipped):** the first divergence is T1–T3 cantrip timing. The searched ranking of early
  cantrips moves because rollout futures now chip-cast Crackle without Hinata.
* **4 (B4, d0/2HG):** 2HG gi 254 and d0 gi 195, 865 and 1773 expose a **pre-existing d0-runner defect**
  the hold had been masking. With the lone Crackle offered, the runner either casts nothing for turns
  (2HG gi 254: Hinata castable from Sol Ring + Izzet Boilerworks + Forbidden Orchard, nothing cast
  T3–T6; it reproduces with the hold disabled entirely, `MTG_NO_HINATA_HOLD_CRACKLE=1`), or casts
  Irencrag Feat and spends its one remaining cast on an Ornithopter instead of the Crackle (gi 865,
  1773). Not root-caused here. Also, `HinataCastableFromHand` counts SOURCES, not mana, so Sol Ring and
  a bounce land under-count it. On gi 254 it read 3 sources (5 mana) as "not castable".

## Still open (the USER's calls; not touched)

* **Lever B** — measured above; the recommendation is to keep it OFF.
* **Staged copies.** `HinataLoneCrackleHeld` counts a Soulfire-staged Crackle as a copy, but a staged
  card is castable only until the end of the next turn. Whether that is the intended "the other stays
  for the combo" is the USER's call.
* Hinata's committed **value leaf and keep table** were fitted to play with the misclassification.
  This fix changes Hinata play (−0.18 turns/game at d0, −0.03 at d3, −0.04 shipped), so both artifacts
  are aged. Nothing was regenerated. Hinata's regression GT keys will move when the tiers are next
  re-run.
'''
s = s[:start] + new
open(p, 'w').write(s)
print('doc patched')
