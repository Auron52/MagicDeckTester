# LandfallElementals: the plan-fan explosion (2026-10-23)

**Status:** DIAGNOSED; one sound fold landed (look-top FLIP fold, `MTG_LOOKTOP_FLIP_FOLD`, default ON);
the remaining explosion is OPEN and blocks the deck's mulligan profile, its Stage 5c2 tie-break check
and its unbudgeted backstop rows on a 23.5 GB box. The bounding fix is a design decision (below).

## 1. Symptom

On a 23.5 GB / 32-core box (RSS cap 20 GB), every pooled LandfallElementals run beyond the small suite
rows aborts at the RSS cap:

* the mulligan generation-setting screen (`mullgen_finalize.py --write`): its d5 b20 hand scorer at 32,
  12 and 6 threads; keep-table discovery at 32 threads (fixed by the pool-allocator retention cap,
  upstream 88a979f07 -- with the pool off discovery peaks at 3.5 GB, K=21);
* `leaf_tiebreak_check.py` (ordinary play WITH mulligans at d5 b20) at 24 and at 10 threads, after
  ~1,100-1,200 of 24,000 games both times;
* unbudgeted (budget 0) games: every d3 block of 10 games abandons 1-4 at the 2M-unit ceiling, and some
  run 17-19 MINUTES without reaching it.

## 2. The two single-game repros (play settings: omit depth/budget -> d5 b20, profile attached)

```
mtg decks/LandfallElementals/LandfallElementals.cod --profile decks/LandfallElementals/LandfallElementals.profile.json \
    --games 1 --seed 1100011 --game-index 11      # MEMORY: 18.9 GB in 23 s, dies at the default cap
                                                  #   (8.4 GB / finishes T5 at d93492f1f~1)
    --games 1 --seed 1000664 --game-index 664     # TIME: 9 min 23 s single-threaded at 611 MB, wins T6
                                                  #   (6:00 with the FLIP fold)
```

Both were in flight when the tie-break batches died. The memory one is ~1 game in 1,100 of ordinary
play; a keep-table generation is ~14M rollouts, so it would meet thousands.

## 3. What the explosion is

ONE main-phase enumeration (`EnumeratePlansWithLand` -> `AppendSubdecisionAxes`) inside a rollout's
per-turn lookahead (FSLineWin -> SimulateToEnd -> RolloutPreCombatSolve -> SolveWithLookahead), turn 6,
three Risen Reefs on the battlefield. gdb at 6 GB: `all` = 1,028,168 plans, `extra` = 440,049 and
growing in the land-search pin fan. `MTG_AXIS_TRACE` stage counts for that list:

| stage | plans |
|---|---|
| base + tutor variants (126k) + land_slot twins (105k) | 200,064 |
| + land-search activation position twins / pre-cast subsets (`land_search_act_slot`, 142k subsets) | 605,538 |
| + land-search pins (which basic each crack fetches) | 1,028,168 |
| + Risen Reef look-top pins: ~605k BASES x 3-4 looks per entrant, ~15-25 one-hot variants each | ~10M (never reached) |

The axes compose multiplicatively because the later fans take the earlier fans' variants as BASES: the
act-slot twins take tutor variants and land_slot twins; the look-top fan takes position twins and tutor
variants (both deliberately, 2026-10-19 loop-1 enumeration fix d93492f1f: "a land-search position twin
IS a base here", because a crack before a reader changes what the reader sees). `MTG_ENUM_HIWATER_KB`
is SILENT for this game -- it reports only a finished enumeration; use `MTG_AXIS_TRACE=<min plans>`.

**Most of it is duplicate work.** `MTG_ENUM_DUPCENSUS` on a turn-6 list of a non-monster game (s70060
gi60): 806,356 plans, 88,030 distinct post-apply states (11%). Per-axis redundancy after the FLIP fold
(variants landing on the same (family, state)): land-search pins 77%, look-top 53%, land_slot 18%,
act-slot 11%.

## 4. Landed: the look-top FLIP fold (sound identity fold)

A look-top LAND ordinal was fanned over the explicit {0 battlefield, 1 hand}; one of the two always equals
the provider's default, so in the additive fan one variant was always the base plan again. It now fans
{-1 default, kLookTopLandFlip}. Same outcome set at every look whatever the default is there. Measured:
the s70060 list 806,356 -> 567,926 plans with the identical distinct-state set (digest equal); 400 games
(seeds 70000-70199) at d3 b10 and d5 b20: 400/400 identical win turns, peak RSS 4.5 -> 1.1 GB (d3) and
4.3 -> 3.1 GB (d5); the time twin 7:24 -> 6:00. It does NOT save the monster (still > 6 GB at ~8 s).

## 5. Options for the remaining explosion (USER decision; nothing below is implemented)

Constraints that bind every option: no greedy pick inside the search window; a budget may re-price work
but never make a line unfindable at large budget; a lossy narrowing needs a held-out A/B and the USER.

1. **Budget-staged look-top answers (largest lever).** Do not pre-fan a one-hot look-top variant for every
   possible look on every base; admit them in a later WAVE (or resolve them through the breakpoint
   machinery, which already handles "the card a look sees"), so a small budget spends on the base plans
   first and a large budget reaches the rest. Sanctioned shape ("waves are the sanctioned way to get a
   lossy lever's win legitimately"). Caveat: at UNLIMITED budget everything is still admitted, so the
   unbudgeted backstop and lazily-materialized lists need the streaming form, not just the ordering.
2. **Size look-top land ordinals by the lands that can actually be seen** (lands left in the library; the
   top window when nothing reorders the library first). Sound in principle, but needs an exhaustive guard
   over every effect that puts cards into / reorders the library -- an incomplete guard makes lines
   unfindable. Would remove most of the remaining 53% look-top redundancy.
3. **Size land-search pins per ordinal** by the sources that can consume that ordinal in resolution
   order (today every ordinal gets the union of every source's value list). Sound if a failed crack
   shifting later ordinals is handled (size each ordinal by the union over itself and LATER sources).
   Targets the 77% land-search redundancy. **MEASURED NULL (2026-10-23):** implemented as a suffix union
   per ordinal (`MTG_LSEARCH_PIN_ORDINAL`, not shipped; patch kept in the agent worktree at
   `logs/fold/proposal3_per_ordinal_sizing.patch`) it removes NOTHING on either repro (567,926 plans
   on/off, identical state set; the monster's stage counts unchanged): Terramorphic / Myriad each offer one
   pick per available basic type, so every source's list is already the same width. The 77% comes from
   resolution-time CLAMPS (a later crack sees fewer picks after an earlier one took the only Island) --
   not knowable at enumeration. It also had an open soundness hole (a FAILED own pre-cast crack consumes
   no ordinal, so a continuation's queued crack shifts onto it).
4. **Bound the act-slot twins x tutor x land_slot cross product.** With a Reef out every Elemental entrant
   is a library-top reader, so no identity fold collapses the positions; it needs staging (as in 1) or a
   measured narrowing -- the USER's call.
5. **A rollout-only narrowing** (the monster sits in the PLAYOUT's per-turn lookahead, beyond the horizon,
   where a greedy policy is permitted): skip the reader/position fans inside RolloutPreCombatSolve. A
   playout-policy change -> held-out A/B; does not help a monster board at a SEARCHED node.

## 6. Instruments added on the way (all inert unless set)

`MTG_EQUIV_ONLY=<probe>:<card>` (one discovery rollout), `MTG_ENUM_HIWATER_KB` axis breakdown,
`MTG_AXIS_TRACE=<min plans>` (stage counts while the list grows), `MTG_ENUM_DUPCENSUS=<min plans>`
(distinct post-apply states + per-axis inert variants; prints and exits).
