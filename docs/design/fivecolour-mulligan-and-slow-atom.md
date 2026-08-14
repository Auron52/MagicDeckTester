# FiveColour: the mulligan run is impractical, and what its slow games told us

2026-08-14. Two findings from the R=1 `--gen-mulligan recommend` scout, plus one general rule.

## 1. ALWAYS merge fetchlands (user rule, 2026-08-14)

Equivalence discovery at the documented threshold (`MTG_EQUIV_THRESHOLD=0.01`) merged **nothing** on
this deck: K=31 raw buckets, every card its own dimension. The fetch cycle is why —

```
Windswept Heath  ~ Verdant Catacombs   0.0125
Wooded Foothills ~ Misty Rainforest    0.015
Scalding Tarn    ~ Wooded Foothills    0.035     <- all five, all ABOVE 0.01
Lightning Greaves ~ Nicol Bolas        0.06      <- must NOT merge
```

Five functionally interchangeable fetchlands each took a dimension, at a measured distance of at most
0.035 turns. The cost of that is not marginal:

| buckets | size-7 hands | vs K=31 |
|---|---|---|
| K=31 (as run) | 5,655,953 | -- |
| K=27, fetch cycle merged | 1,977,898 | **2.86x smaller** |
| K=20, + duals merged | 351,480 | 16x -- but see below |

**The rule: fetchlands always merge, as a CLASS, regardless of threshold.** They are a known
equivalence, and leaving it to a distance threshold means a deck-by-deck coin flip on a 2.9x cost
difference. A threshold of 0.04 happens to do it here (merging the cycle and stopping short of
Greaves~Bolas at 0.06) but that is luck, not a rule.

**Do NOT extend this to duals.** Their distances (Steam Vents ~ Mountain 0.0725, Island ~ Steam Vents
0.0775) are ~5x the fetchlands' and that is real signal: this deck plays Faeburrow Elder and Bloom
Tender, whose output scales with the COLOURS you control, so which dual you hold genuinely matters.
The 16x is a trap.

Caveat on the fetches themselves: they are not strictly interchangeable either (different colour
pairs), so merging is an approximation bounded by the measured 0.035. It is accepted because the deck
runs enough duals to cover most fetch targets.

## 2. The profile is impractical for this deck either way

At K=31 the scout reached 25.2% of the size-7 phase in 7h11m on 23 cores (1,426,276 of 5,655,953
cells at 55 cells/s), i.e. **~21 more hours for the R=1 floor pass alone**, before the 3,097,330 fused
sub-table batches. Merging fetches brings the whole pass to roughly 10 hours -- still too long
(user). And R=1 is not shippable: **R < 10 cannot produce a runtime profile** at all.

So FiveColour stays on mulligan defaults. This is the K=31-with-1-ofs case the skill's feasibility
guide calls out; the deck is a 60-card singleton-heavy pile and is simply not compressible enough.

## 3. What the slow games DID buy: the atom is variable multi-colour mana

253 slow rollouts (>=30 s), 506 minutes total, **worst single rollout 1,851 s -- 31 minutes**. Ranked
by share of slow-rollout time:

```
Bloom Tender        198 hands  25,602 s   78.3% of slow hands
Mountain            103        15,759     40.7%
Blood Crypt          95        15,273     37.5%
Overgrown Tomb       91        12,509     36.0%
Faeburrow Elder     115        11,376     45.5%
```

Top co-occurrence: **Bloom Tender + Faeburrow Elder (85 hands)**.

Those two are the SAME effect -- *"{T}: Add one mana of each color among permanents you control"* --
a mana source whose output is a function of board state rather than a fixed set. Everything else on
the list is a land that feeds them. So the ranking is not five separate problems; it is one:

> **Mana-payment enumeration over VARIABLE-OUTPUT sources is the degenerate atom.**

This corroborates the independent `perf` profile of the depth-matrix tail (flat profile, mana payment
the largest coherent cluster at 22.3%, one stuck thread at 39% in mana backtracking) from a completely
different measurement path. Two routes, one answer.

**Why it matters beyond this deck:** the same class appears wherever a deck plays Bloom Tender /
Faeburrow Elder / Chromatic Lantern-style effects, and the cost is combinatorial breadth, which no
micro-optimisation touches. A fold or memo keyed on the *realised colour set* rather than the source
permutation is the shape of the fix -- the sibling tap-backtrack collapse is the precedent.

Reproducers: every slow line in `logs/fc_mull/recommend.log` carries its seed and exact hand, and the
run also writes `<raw>.slow.log`.

Related: `depth-matrix-degenerate-games.md` (the perf profile and the per-game abort),
`.claude/skills/mulligan-profile.md` (feasibility guide), `mana-source-reservation.md`.
