# Goblins value model: isolating its effect from the GT drift

**Status: IN FLIGHT 2026-08-01.** Smoke + regression GT rebaselined (`e36f3bb`); **overnight GT NOT
accepted** and should not be accepted on goblins evidence alone (see *The confound*). A 260,000-game
isolated A/B is running; where it lands and how to read it is at the bottom.

Context: `8a3bbb1` added `decks/Goblins/Goblins.value.json` (a learned value model plus the adopted
`value_play` config). Four goblins cases in smoke/regression and twelve in overnight then failed, and
the question was whether the model is a quality regression at searched depths.

## The confound: a GT diff is not an A/B

The overnight failures looked like a searched-depth regression (+0.0030 at d3 and d5). They are not
attributable to the value model, because the goblins **overnight GT was accepted at `9462479`**
("re-accept overnight GT after land-tiebreak rebase") — which predates *six* engine commits, among
them `ec70359`'s search-perf heuristics. Comparing today's binary against that GT measures all of it
at once.

**Check GT provenance before attributing a diff:**

```bash
git log --oneline -3 -S "<deck>_<mode>_d<depth>_s<seed>=<the old value>" -- test/regression_gt.txt
git log --oneline <that sha>..HEAD -- src/ai src/core src/cards     # what else moved since
```

The clean measurement holds the binary fixed and toggles only the thing under test:

```bash
cp -r decks/Goblins /tmp/gob_nv/ && rm -f /tmp/gob_nv/Goblins/Goblins.value.json
# both arms, ONE pooled batch, job shape mirroring test/regression.sh's manifest emitter
```

Results on 12 fresh seeds (12,000 games per arm per depth), metric = loss-penalized avg
(unwon scored `max_turns+1` = 9):

| depth | slower | faster | net | sign test |
|---|---|---|---|---|
| d3 | 3 | 3 | +0.0000 | p = 1.00 |
| d5 | 42 | 65 | **−0.0018** | p = 0.033 |

d3 is inert **because the d3 job pins `depth=3 ignore_play_profile=true`, which bypasses the
`value_play` block entirely**. The 52 changed d3 games the overnight showed therefore come from the
engine commits, not the model.

## Four ways to measure the wrong thing here (all hit during this investigation)

1. **`classify_turn_later.sh` only re-runs the NEW arm.** It reports `churn` vs `PERSISTS` for a
   searched slowdown, but it never asks whether the OLD side's faster win survives more search. When
   the old side is itself budget luck, `PERSISTS` reads as "real regression" when the truth is the
   opposite. `goblins_smoke_d5_s1001` gi43 was exactly this: T5 → T6 at the 20 ms case budget,
   `PERSISTS` at 4× and 16× — but the *old* arm also becomes T6 at 80 ms and stays T6 at unlimited.
   **Run BOTH arms at `--budget-ms 0` before calling a searched slowdown real.**
2. **`--budget-ms 0` means unlimited; a big number does not.** The budget is a deterministic *virtual*
   work-unit count (`SearchBudget::NODES_PER_VIRTUAL_MS = 900`), and `<= 0` disables the limit. A
   20480 ms budget is still bounded. The budget being work-units, not wall clock, is also why these
   runs reproduce under any machine load.
3. **The d5 case OMITS the depth key so `value_play` owns the depth — so the two arms can run at
   DIFFERENT depths.** Here: baseline d5, value-model arm d6 (its `value_play.target_depth`). The d5
   case is not "leaf on/off", it is "d5 heuristic" vs "d6 + leaf + fallback crossover".
   **`explain_game.py` pins `--depth <case depth> --ignore-play-profile`**, which bypasses
   `value_play` and silently diagnoses a configuration the case never ran. Reproduce a d5 case by
   OMITTING `--depth` and passing only `--budget-ms` (matches `regression.sh`'s emitter and the
   `d5 repro` note in the play-viewer findings).
4. **`explain_game.py --old-bin` resolves the deck from the CURRENT tree.** Fine for a code change,
   useless when the change is a deck-sibling data file: the old binary still picks up the new
   `value.json` and reports "no change". Use a deck-copy A/B instead.

Also worth knowing: **`value_play` may live in `<deck>.value.json` OR in the profile** —
`MulliganProfileIO.h` reads `value_play` from the model's meta first, then the top level. Grepping
only the profile will tell you a deck has no play config when it does.

## The run in flight

```
test/logs/goblins_valuemodel_big/     manifest.json, batch.log, wins/<job>.wins
  d5: 100 fresh seeds (100001..100100) x 1000 games x {old,new}
  d3:  30 fresh seeds (200001..200030) x 1000 games x {old,new}      = 260,000 games
```

Seeds are disjoint from every suite tier (1001 / 2002 / 3003 / 4004–7007), from the 12-seed sweep
(11011..23023), and from the reserved gen seeds. Analysis is a paired per-game diff of
`wins/old_<depth>_s<seed>.wins` against `wins/new_<depth>_s<seed>.wins`, scoring an unwon game 9, plus
a two-sided sign test over the changed games. At d5 expect ~9 changed games per 1000, so ~900 changed
in 100,000 → s.e. of the mean delta ≈ 0.0003, i.e. the −0.0018 becomes ~6σ if it is real.

## Open

- The overnight GT is stale for reasons beyond goblins (six engine commits). Re-accepting it is a
  decision for whoever owns those changes, not a goblins-only call.
- Their own validation measured d6/b40 against a **d5/b20-with-fallback** baseline (−0.0033t); the
  suite's d5 baseline is plain d5 at the case budget, so the suite is not measuring their comparison.
