# Unite the Coalition split — resume state (2026-08-09)

**Status:** heuristic ADOPTED and live (`ca9c50b`), honest lethal detector landed (`14185a0`).
One decision outstanding, then a rebaseline.

## The outstanding decision

`MTG_FIVEC_UNITE_ALLIN` — whether the non-lethal branch keeps the all-in split.

* `1` (current default) emits `{0,1,2,5}` when the lethal test does not fire.
* `0` emits `{0,1,2}`, which is the user's rule in its original form.

It defaulted to 1 because, **before** the honest detector, dropping the all-in deleted the finish
from the lookahead and cost a turn (s6006 gi209 — see `14185a0`). With the detector fixed that game
is byte-identical to the full search at *both* settings, and `ALLIN=0` was the cheaper arm on that
seed (317,989 vs 324,870 core-ms). One seed is not enough to remove a safety net, so the 5-seed A/B
was launched:

```bash
for allin in 0 1; do
  MTG_FIVEC_UNITE_SPLIT=1 MTG_FIVEC_UNITE_ALLIN=$allin \
    build/Release/mtg --batch /tmp/unite_ab.json --threads 12 > /tmp/hd$allin.txt 2>/dev/null
done
```

`/tmp/unite_ab.json` = seeds 2002/3003/5005/6006/7007, 250 games each, d3/b10. Rebuild it with
`scripts/deck_registry.py` paths if the tmp file is gone. **Search baseline is reusable**
(`MTG_FIVEC_UNITE_SPLIT=0` takes the untouched `GenericProvider` path):

| seed | 2002 | 3003 | 5005 | 6006 | 7007 | mean | core-ms |
|---|---|---|---|---|---|---|---|
| search | 5.0320 | 5.1160 | 5.1080 | 5.1120 | 5.1040 | 5.09440 | 2,373,727 |

**Decision rule:** sum across all five seeds; a per-seed sign flip at ~0.008 is noise, not signal
(this session produced two single-seed reads a wider run overturned). If `ALLIN=0` is not worse on
any seed, drop the net — it is the cheapest arm and the cleanest form of the rule. If it is worse
anywhere, the detector still has a blind spot; see below.

## Known remaining blind spot in the detector

It measures **attack** reach only. Non-combat damage is invisible to it — notably **Mana Cannons**
(`multicolor_cast_damage_per_color`), which deals X = the cast spell's colour count and is worth **5**
off a Unite cast. On the s6006 gi209 turn-5 line, lethal was Unite's 10 **plus** a Mana Cannons
trigger for 5; the split came out right only because the ≥2-castable-threats clause fired instead.
If `ALLIN=0` loses a game anywhere, look here first: add the same-turn cast-trigger damage the plan
would generate.

## Then: rebaseline

FiveColour is in **no** regression tier, and smoke (30/30) / regression (50/50) are green, so no
ground truth needs regenerating. What needs it is the **value-leaf matrix**, which was completed
(52/52 cells, 20,800 games) *before* `14185a0`:

```bash
MTG_SLOW_GAME_MS=30000 MTG_SLOW_GAME_LOG=logs/vlq_fivecolour/slow_games.log \
python3 scripts/attic/valueleaf_depth_matrix.py --incremental --decks fivecolour_staged \
  --hdepths 1 2 3 4 5 --vdepths 1 2 3 4 5 6 7 8 --seeds 8008 9009 10010 11011 \
  --target 400 --reference-target 50 --batch 25 --workers 20 --value-min-depth 0 \
  --intractable-sec-per-game 60 --never-condemn-at-or-below 5 \
  --out logs/vlq_fivecolour/matrix.txt
```

The resync is automatic and chunk-granular: it drops only the chunks whose engine differs and
re-runs them, ~117 games (0.6% of the table), minutes of wall. At `ALLIN=1` the s6006 digest was
unchanged by `14185a0` (`2629bf15`), so the table may well be unaffected — but that is 250 games on
one seed, not proof, and the resync is cheap enough not to argue about.

Phases D (crossover → `value_trust_depth`) and E (staged-vs-live A/B) still have to run:
`bash scripts/valueleaf.sh run decks/FiveColour` skips the finished phases.

## The completed matrix, for reference

```
heuristic:  H1=5.0981[540ms]  H2=5.0565[5.9s]  H3=5.0366[36s]  H4=5.0236[160s]  H5=5.0165[348s]
value-leaf: V1=5.4698[9ms] V2=5.2734[59ms] V3=5.1703[379ms] V4=5.0707[2.1s]
            V5=5.0279[5.8s] V6=5.0217[7.1s] V7=5.0217[7.0s] V8=5.0204[9.4s]
```

V6 matches H4 for 23x less work; V5 sits between H3 and H4 at 6x less than H3. The deep H cells keep
improving but the return collapses — H5 costs 2.2x H4 for a third of the gain H4 gave over H3.
