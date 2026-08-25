# A board-scaled LAND is invisible to ManaSourceRank's reserve tiers

**Status: defect CONFIRMED and root-caused; fix BUILT behind `MTG_SCALED_LAND_RANK` (default OFF);
adoption NOT taken — measured aggregate-neutral.** Self-contained.

## The defect

`ManaSourceRank` reserves every source whose value grows with the board:

| tier | source |
|---|---|
| 60 | animated manland (Mutavault) |
| 61 | scaled mana dork (Priest of Titania, Elvish Archdruid) |
| 62 | storage land (Dwarven Hold, Mercadian Bazaar) |
| 63 | live untap-burst land (Wirewood Lodge + a 2+ scaled Elf) |

There is **no tier for a board-scaled LAND**, because the predicate is creature-gated and
zero-feeder-gated:

```cpp
inline bool IsScaledManaDork(const CardDefinition& def)
{
    return def.card.IsCreature()                              // Three Tree City is a LAND
        && !def.params.mana_per_creature_subtype.empty()
        && def.params.mana_per_creature_feeder_generic == 0;  // Three Tree City's feeder is 2
}
```

Three Tree City (`{T}: Add {C}` / `{2},{T}: Add {R} equal to your Goblins`) fails both guards and
falls through to the plain colour ladder. Since `eaccc120` gave a `{C}`-only source rank **5**
("least flexible -> spend FIRST"), the highest-yield source on the board became the **first** one
tapped. The rank reads `EffectiveProduces`, which only ever sees the basic `{C}` mode.

The engine already carries the right predicate — `IsScaledManaLand()` and `ScaledManaNetYield()`
(live + affordable + beats the basic tap) — used by the payment layer and the solver. The rank
simply never calls them.

## The measured game

Goblins d0 `s4004` gi90 (`--seed 4094 --game-index 90 --depth 0 --budget-ms 0
--ignore-play-profile`). T4, four lands (3 Mountain + Three Tree City), five Goblins on board,
opponent at 11. Both arms cast Aether Vial for `{1}` in main 1 and attack for 8 (opponent to 3).

| | pays the Vial's `{1}` | main 2 |
|---|---|---|
| pre-`eaccc120` | a Mountain | city still up -> `{2}` in, **5 red** out -> **two** Siege-Gang activations (`{1}{R}` each), 4 damage, **win T4** |
| post-`eaccc120` | **Three Tree City**, basic `{C}` mode | 3 Mountains left -> **one** activation, opponent survives at **1**, win T5 |

Net yield of the scaled mode with 5 Goblins is `5 - 2 = +3` against the basic mode's `+1`. The rank
spent a five-mana ability to pay one generic pip.

`eaccc120` is the first bad commit by bisect (`31beff1a` good -> `eaccc120` bad, test = gi90 wins
T4). Its own write-up recorded goblins d0 moving `4.1040 -> 4.1050` and classified it as churn
inside a net `-0.0390`; the mechanism above shows it is not churn.

## The fix, and why it was NOT adopted

One line in `ManaSourceRank`, gated exactly as tier 63 gates on `UntapLandBurstNet(...) > 0`:

```cpp
if (ScaledLandRankEnabled() && ScaledManaNetYield(s, def) > 0) { return 61; }
```

Blast radius is provably one deck: Three Tree City is the only card in `cards.json` with
`mana_per_creature_feeder_generic > 0`, and only Goblins holds it.

**A/B, all three tiers, one pooled batch, 19,325 games per arm** (`logs/gb_scaled/`):

```
weighted mean ON-OFF = -0.00005 turns
every SEARCHED cell (d3, d5, all seeds, all tiers): 0.0000 change
d0 only:  overnight_d0_s4004 -0.0005 (reaches GT exactly)
          regression_d0_s2002 -0.0010 (restores the pre-eaccc120 4.1040)
          overnight_d0_s10010 +0.0005
          overnight_d0_s6006 / d0_s8008  0.0000
```

Per-game it recovers **2 of the 4** regressed games (gi90 4, gi1011 5); gi1406 and gi1672 are
unmoved (different cause). Goblins' overnight net against GT is **+1 turn either way** — the fix
trades which d0 games move, it does not change the deck's aggregate.

So: the defect is real and the fix is mechanically correct, narrow, and free — but it buys nothing
measurable, and **the search is already immune** (every searched cell is byte-identical, because the
lookahead finds the right tap order regardless of the greedy rank). Adopting a default-flip plus a
GT rebaseline for a `-0.00005` aggregate is not justified on the repo's own adoption bar.

The lever is kept **default OFF** so the finding is reproducible and the branch is one env var away
if a future deck makes a scaled land matter at searched depth. If a second scaled land ever enters
`cards.json`, re-run this A/B before assuming the result carries over — it was measured on a deck
whose search already compensates.

## Flag verification (and a trap in the prescribed one)

`coding-conventions.md` prescribes clean-env / `=0` / `=1` smoke runs, the third of which must
DIVERGE. Here it does not, and that is not a dead lever:

```
clean-env smoke              42/42, configs changed 0     (inert by default -- correct)
MTG_SCALED_LAND_RANK=0 smoke 42/42, configs changed 0     (=0 means off -- correct)
MTG_SCALED_LAND_RANK=1 smoke 42/42, configs changed 0     <- smoke CANNOT see this lever
```

Smoke's single seed contains no binding game. Liveness must be shown on the **regression** tier,
and both read paths were checked to agree:

```
goblins_regression_d0_s2002   lever OFF          avg=4.1050 digest=7dcb6aa3f9150a56
                              env    =1          avg=4.1040 digest=4c433c14d8dfec57
                              manifest "flags"   avg=4.1040 digest=4c433c14d8dfec57   <- identical
```

Generalise: when a lever's blast radius is one deck, the smoke tier may hold no binding game, and a
`=1` smoke that shows "0 configs changed" then looks exactly like a lever that was never wired up.
Always pin liveness on a cell you have *measured* to bind, and always compare the env and manifest
digests — a silently-dead manifest arm reads as "the lever has no effect".

## Reproduce

```
bash build.sh
MTG_SCALED_LAND_RANK=1 MTG_DUMP_WINS=1 ./build/Release/mtg decks/Goblins/Goblins.cod \
  --profile decks/Goblins/Goblins.profile.json --games 1 --seed 4094 --game-index 90 \
  --threads 1 --depth 0 --budget-ms 0 --ignore-play-profile      # wt=4 (GT); unset -> wt=5
```
