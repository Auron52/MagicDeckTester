# kittyv2's play digest is not reproducible under `--batch` at high thread counts

**Status:** OPEN. Found 2026-10-04 while localising the `claude_s5_gi4` win-turn shortfall. **Present on
committed `52b4878f`, so it is not introduced by that investigation's changes.** It matters beyond
kittyv2 because it means this deck's ground-truth digests are samples, not fingerprints.

## The claim

One job, one manifest, identical settings, three runs of the **committed** binary:

```
$ mtg --batch one.json --threads 8          # committed 52b4878f
kittyv2_smoke_d3_s1001: played=100 avg=4.1600 digest=c1cc885b74b9892e ms=210965 units=1678964
kittyv2_smoke_d3_s1001: played=100 avg=4.1700 digest=d69192eaa5413885 ms=210731 units=1677887
kittyv2_smoke_d3_s1001: played=100 avg=4.1700 digest=d69192eaa5413885 ms=210424 units=1677553
```

Two distinct digests, both averages, and **`units` differs on every run** — so the search genuinely
explores differently; this is not a reporting artifact. The manifest is the smoke config lifted
verbatim out of `test/logs/smoke/manifest.json`:

```json
{"jobs":[{"name":"kittyv2_smoke_d3_s1001",
          "deck":"decks/KittyEquipment/v2-puresteel-hammer/KittyEquipment.cod",
          "profile":"decks/KittyEquipment/v2-puresteel-hammer/KittyEquipment.profile.json",
          "games":100,"seed":1001,"depth":3,"budget_ms":10,
          "ignore_play_profile":true,"weight":0}]}
```

## What it is bounded by (all measured, same manifest)

| arm | result |
|---|---|
| `--threads 1` | **deterministic** — identical digest AND identical `units`, 2/2 |
| `--threads 2` | **deterministic** — identical digest 3/3, and it agrees with `--threads 1` |
| `--threads 8` | **non-deterministic** — 3 runs, 2–3 distinct digests, `units` differs every run |
| `--threads 8`, `MTG_EQUIP_ROUTE=0` | **deterministic** — identical digest AND `units`, 2/2 |
| `--threads 8`, `MTG_EQUIP_ROUTE_MEMO=0` | still non-deterministic (2 distinct digests) |

So it requires **the equip-chain go-off route** and **more than two worker threads**. Turning the
route off is 10x cheaper on this config (29 s vs 270 s of core time), which is why the obvious
suspicion is a cost-pressure-dependent path rather than the route's logic as such.

## The committed ground truth cannot be reproduced by the binary that produced it

`test/regression_gt.txt` records `kittyv2_smoke_d3_s1001=4.1700/87dac5763e98236e`. The committed
binary run single-threaded — i.e. in its one *deterministic* configuration — gives:

```
avg=4.1600 digest=bc1f78d234c2bb68 units=1681328
```

Different digest and a different average. The recorded GT is one draw from the multithreaded
distribution, so **`--accept` on this config cannot converge**: any rebaseline just records another
sample. That is the practical consequence and the reason this is written up rather than rebaselined
around.

## What has been ruled out

* **The route's verdict memo.** `MTG_EQUIP_ROUTE_MEMO=0` (added in the same batch of work) still
  varies. The memo *is* separately unsound — `comboroute::BoardKey` hashed `aura_attached_to` and not
  `equipped_to`, so attachment-only board differences shared an entry — and that is fixed, but it is
  not this.
* **Condemnation and the wall-clock backstops.** `BatchRunner`'s `condemn.enabled` requires
  `median_sec_per_game > 0 && reference_games > 0` or `max_game_sec > 0`; this manifest sets no
  `condemn` block at all, so `frozen`/`late_now` are 0, `ceiling` falls back to `job.abandon_units`
  (also unset, so 0 = disarmed), `ArmDeadline` is never called, and `PublishTo` is gated on
  `is_calib && frozen == 0`. None of the cross-thread limit machinery is armed here.
* **`SearchBudget` reading a clock.** It is deterministic virtual ms by construction; the
  steady_clock deadline it used to carry was removed precisely to make the search reproducible.
* **The enumeration memo.** `enummemo::t_cache` is `thread_local` and epoch-checked per decision.
* **A reporting-only artifact.** `units` is a deterministic work counter and it moves.

## Where to look next

The remaining shape is "something shared, or something address/allocator-dependent, that the route's
volume makes reachable". Two concrete candidates, in order:

1. **An iteration order that depends on a pointer.** Any `unordered_map`/`unordered_set` keyed on a
   pointer (`const CardDefinition*` is the obvious one) iterates in an allocator-dependent order,
   and allocator behaviour changes with thread count. A choice made by walking such a container
   would produce exactly this signature: same inputs, different search path, different `units`.
2. **A data race on a non-atomic shared counter that feeds a decision** (as opposed to the many
   `std::atomic` telemetry counters, which cannot change play). The route added new shared state;
   the diagnostic counters it touches (`keparkstats`, `FreeEquipDiagNote*`) should be checked for
   whether anything reads them back.

A good first instrument is a two-run `units`-per-game diff at `--threads 8` to find the FIRST game
whose work diverges, then that game alone at `--threads 1` vs inside the 8-thread pool.

## Why this is not just a tidiness problem

* `.github/workflows/build.yml` runs a **determinism parity** job asserting Linux and Windows agree
  for the same seed. A deck whose own digest is thread-dependent can fail or pass that job by luck.
* It undermines digest-level A/B on this deck, which is the repo's primary neutrality instrument
  (`digest-equality-beats-a-sign-test`). Aggregate win-turn comparisons are unaffected when they are
  larger than the noise: the `scripts/ref_bench.py` route is **reproducible** (14 one-game jobs,
  forced mulligans: 1/14 short, avg 3.500, twice), which is why the win-turn work alongside this
  finding still stands.
* The 2026-10-03 adoption notes for the route ("all three kittyv2 configs better") rest on an
  average difference larger than the +-0.01 seen here, but their **digests** should not be treated as
  reproducible.

## 2026-10-05 addendum: a SECOND deck shows the same signature (selesnya d3)

The byte-identity smoke for the Ardenn change (`2f66f29c`, param-gated, no suite deck holds the
card) came back 109/110 rows identical to the bf67c6fa freeze. The one mover was NOT a kitty row:
`selesnya_smoke_d3_s1001` went `5.4467/7b7d9cfae5c5d0ec` (freeze AND committed GT) ->
`5.4533/c93175813744d40a` in the pooled 32-thread run. A 2x2 at `--threads 1` -- {bf67c6fa, 2f66f29c}
binary x {without, with Ardenn} `cards.json`, the selesnya job alone -- produced
`5.4467/7b7d9cfae5c5d0ec` in all four cells. So neither the code nor the data moved it: it is the
pooled run's draw, and SelesnyaLifegain (its own provider; no equip route) is now a second deck on
which the pooled digest is not reproducible. The candidate list above is unchanged, but the
equip-route-specific guesses are weakened: whatever it is, it is reachable from a deck that never
enters that route. The same test (job alone at `--threads 1` under both binaries) is the cheap
identity proof to reach for whenever a single searched row moves in a pooled tier.
