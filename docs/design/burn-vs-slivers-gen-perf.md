# Burn vs Slivers exhaustive-keep generation perf gap (~6×)

Status: **open, queued investigation** (requested 2026-07-10). Not blocking any
adoption; framed here with the measured throughput and a leading hypothesis so a
perf pass can start from data rather than a cold start.

## The observation

Both decks generated an R=40 adaptive (floor=20) exhaustive-keep profile on the
same 24-thread machine, same generator (fixed sampling, HEAD ~5aa7573):

| deck    | rollouts | wall time | throughput | per-rollout (÷24 threads) |
|---------|---------:|----------:|-----------:|--------------------------:|
| slivers | 880,376  | 6,103 s   | ~144/s     | ~167 ms                   |
| burn    | 670,832  | 26,518 s  | ~25/s      | ~960 ms                   |

Burn is **~5.8× slower per rollout** despite being the simpler deck (mono-red,
few permanent types, no affinity, no Vial engine). The prune is **active** on burn
(no affinity) and **bails out on Hivepool turns** for slivers — so the prune makes
slivers *slower*, not faster, and therefore is not the driver of the gap. The gap
is in the rollout playout itself.

## Leading hypothesis — rollout horizon (durdle-to-8 vs early lethal)

A rollout plays a kept hand out until a win or `--max-turns 8`. The two decks hit
very different average horizons:

- **Slivers** deploys creatures (Cavern/Vial into Galerider/Predatory/Muscle
  Slivers) and reaches lethal fast — the keep A/B shows avg win turn ~4.4. Most
  rollouts **terminate early** (turn 4-5), simulating far fewer turns.
- **Burn** is the deck whose *land-light* hands the exhaustive profile is built to
  evaluate. In a goldfish with no opponent to kill and no lethal without mana,
  those land-light hands **durdle to the full turn-8 horizon** every time
  (memory measured ~1.4 s/rollout for the durdling land-light buckets). The
  generator deliberately spends most of its rollouts on exactly these land-light
  cells, so the expensive tail dominates the aggregate.

Horizon alone (8 vs ~4.5 turns ≈ 1.8×) does **not** fully explain 5.8×, so a
**per-turn cost** component is almost certainly also present: on a burn durdle turn
the solver still enumerates the (unaffordable) burn spells in hand every turn, and
land-light hands keep more spells in hand for more turns, so the per-turn
enumeration/search may itself be heavier on burn than the intuition "durdle = do
nothing" suggests.

## Disambiguating measurement (when picked up)

1. **Turn-horizon histogram per rollout, burn vs slivers.** Instrument the rollout
   to record turns-simulated; confirm burn's mass sits at turn 8 and slivers'
   at ~4-5. This bounds the horizon contribution.
2. **Per-turn wall time on a burn durdle turn vs a slivers develop turn.** If burn
   per-turn is *not* cheap, the target is the durdle-turn solve (enumerating
   uncastable burn spells every turn); consider a fast-path when no candidate is
   affordable (the mana-total prune already skips unaffordable *combos*, but the
   per-turn machinery around it still runs).
3. **perf/profile a burn durdle-heavy scorer workload** (as in
   [[mana-total-prune-enumeration]]'s durdle scorer) to see whether the self-time
   is in `TurnSolver::Solve`/`EnumeratePlans` (per-turn) or in the GameState
   deep-copy per plan-apply (per-turn count × horizon).

## Likely levers (pre-judged, verify before adopting)

- **Low-mana / no-affordable-candidate fast-path** in the per-turn solve for
  durdle turns — the biggest expected win if per-turn cost is the driver.
- **GameState deep-copy per plan-apply** — already the known hotspot
  ([[hinata-perf-profile-2026-07-01]], optimization-backlog); its cost scales with
  turns × surviving-plans, so burn's long horizon amplifies it.
- **A land-floor / early-durdle cutoff** for clearly-dead land-light hands would
  cut rollout length, but changes evaluation semantics (a keep-policy decision, not
  a pure perf change) — must go through the mulligan-profile A/B, not adopted as a
  perf tweak.

## Note

This does not affect correctness or the NEW-vs-OLD profile comparison; it only
determines how long a burn regen takes (burn R=40 ≈ 7.4 h here). Raising burn to
R=60 as a sampling-quality fallback would cost proportionally more, which is part
of why R=40 was chosen for the directional read.
