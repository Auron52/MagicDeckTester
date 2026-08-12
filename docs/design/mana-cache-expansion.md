# Payable mana cache — expansion surface (deferred)

**Status:** the base cache is adopted (commit `perf(mana): payable-mana cache …`, default ON via
`EnvOn("MTG_MANA_CACHE", true)`). This doc records the cases it deliberately does **not** cover yet, so
a future pass can extend coverage without re-deriving the design. Nothing here is in-flight.

## What is cached today

The cache memoises the **`out_full_pool` batch-prepay** backtracker solve (the residual-insensitive
path, ~99% of solver nodes) → `{payable, produced pool, tap-set}`, keyed by a 128-bit hash of the
active player's mana-source configuration + the full `ManaCost` + shape args (`for_creature`,
`reserved_mask`, `untapped_max`). A hit re-taps the stored sources and returns the pool. It is
**byte-identical** (validated: SMOKE 30/30, REGRESSION 50/50 digests, 0 play-changed, 184 refs 0 drift).

Covered sources include plain lands/dorks/rocks, **City of Brass** (its `tap_self_damage` is replayed on
the hit) and **Reflecting Pool** (its effective colour set — a function of the other lands — is hashed
into the key so distinct reachable-colour boards get distinct entries).

Three gates keep it off in the rest (see the header comment on the cache in `src/core/SpellEffects.cpp`):

- **SHAPE** — only the canonical batch-prepay call (`out_full_pool` set, `out_leftover`/`rp_colors` null,
  empty floating, board ≤ 64 permanents). The colour-floating `out_leftover` path (~1.3% of nodes) is
  never cached.
- **GLOBAL (`McStateDependent`)** — the whole node is skipped when any active source has state-dependent
  branching the key can't capture: **domain** (colours among all permanents) or **scaled** (creature
  count).
- **STORE (`McReplayable`)** — a miss is stored only if every tapped source is replayable by the hit
  path. **Depletion**, **storage**, and **drip** (`tap_opponent_lifegain`) lands are not — they mutate
  counters/opponent-life the hit path doesn't reproduce — so those solves are recomputed each time.

For the Creature Giving deck none of the off-cases apply (no domain/scaled/depletion/storage/drip
sources), so the cache is fully active there — this is the deck the base win was measured on.

## Expansion opportunities (ranked by how the win was found, not yet measured)

Each is a *deterministic* extension, so the validation bar is unchanged: **byte-identical** across the
full regression suite (50/50 digests + refs 0 drift) with the extension on. Measure the node/wall win
per extension before adopting; extend the same `EnvOn` gate or add a sub-flag for the A/B.

1. **Domain / scaled boards (GLOBAL gate).** Mirror the Reflecting-Pool treatment: instead of skipping
   the node, hash the *state the DFS reads* into the key — for `domain_mana`, the domain colour set
   (colours among all the controller's permanents); for a scaled land, the creature count. Then the
   board is cacheable and a hit is still byte-identical. Watch for other permanents changing the domain
   set mid-turn (tokens, sacrifices) — the key must be recomputed per call (it already rescans sources).

2. **Depletion / storage / drip storage (STORE gate).** Make the hit path *replay* their tap effect the
   way City of Brass's damage is replayed: decrement the depletion counter / spend the storage counter /
   apply the opponent lifegain on the stored tap. Then those solves become storable. Each needs its
   effect reproduced exactly on the hit (and the pre-tap counter value folded into the key so two boards
   with different counters don't collide).

   **DEPLETION HALF DONE (2026-08-12, the Mirrorwing enablement).** Depletion lands (Sandstone
   Needle) are now storable: the hit replays `DecrementDepletionOnTap` exactly. No key addition was
   needed — production is counter-independent and the decrement is relative, so a stale counter value
   cannot exist. Measured on Mirrorwing (3x Needle): cache coverage 60% → 72% of batch-prepay solves,
   worker nodes −45% vs the base cache (−80% vs cache-off) at d3 b20 x100. Byte-identity revalidated:
   regression 55/55 digests + 186 refs 0 drift + a 100-game single-threaded cache-on/off diff on
   Mirrorwing itself. Storage and drip remain unstored: storage needs its counter value in the key
   anyway (now hashed — see below), and a drip solution's *mode* ({C} vs coloured tap) is not captured
   by a tap-set, so its replay is ambiguous — leave it recomputed.

## Key holes fixed (2026-08-12) — latent stale-hit bugs, not expansions

The original key hashed each source as `(index, def, tapped)` plus the cost/shape args. Four pieces of
state the DFS *reads* were missing, so two boards could share a key yet solve differently — a stale
hit would then replay the wrong answer. All four are now hashed (byte-identical-or-better; the suite
already passed, and key refinement only turns wrong-hits into misses):

- **Dork tap-eligibility** (`CanTapNow`: summoning sickness / `temp_haste` / lord+equip haste). The
  reachable case is Mirrorwing's Expedite mid-turn (a sick dork becomes tappable with no change to
  def/tapped) and any cross-turn sickness flip on an otherwise-identical board (Creature Giving's
  Birds).
- **Storage battery** (`storage_counters` + `storage_hold_this_turn`): burst amount and
  `StorageSourceLive` both read it; a stored *negative* went stale when the battery charged.
- **Deathrite fuel** (`GraveyardLandFuel` when a `gy_land_exile_mana` source is present): production
  gates on graveyard land count the source scan cannot see. Deathrite is also now excluded from
  `McReplayable` — its tap EXILES a graveyard land, which a tap-set replay cannot reproduce (a stored
  positive previously skipped the exile on every hit: N Deathrites off one land, the exact bug class
  fixed once already in the worker).
- **Drip usefulness** (`OpponentLifegainUseful` when a drip source is present): the worker's branch
  ORDER depends on it, so the found solution's colour composition could differ across boards sharing
  a key.

3. **`out_leftover` colour-floating path (SHAPE gate).** Residual-sensitive — the produced *leftover*
   colours matter to the caller, so the cached pool must capture the exact colour breakdown, not just
   payability. Higher risk; only worth it if profiling shows this path is hot on some deck (it was ~1.3%
   of nodes on Creature Giving).

4. **Cross-node retained / incrementally-invalidated cache (the "crazier" variant).** Today the map is
   `thread_local` and grows within a rollout, cleared at 500k entries; the key is recomputed each call.
   The floated idea is to *retain* solutions across nodes and only invalidate on a board change (tap
   state, source set, counters), rather than re-keying every call. Trickier/riskier: correctness hinges
   on a complete invalidation set (every input the key hashes must trigger invalidation). Prototype
   behind a flag and hold it to the same byte-identical bar before trusting it; the per-call re-key is
   cheap enough that this only pays off if re-keying itself shows up in a profile.

## Measuring which one is worth it

The reuse/coverage measurement that motivated the base cache was a diagnostic probe over the backtracker
(`MTG_TAP_KEYSTATS`) that counted, per node: cache-reuse at several key granularities (full key / query /
source-config / cost), the fraction of nodes cacheable under the current gates, and the
`out_full_pool` vs `out_leftover` vs neither split. Rebuild an equivalent probe (a set of
`thread_local` counters incremented in `TapForCostBacktrack` under a `MTG_*` diagnostic flag, dumped at
teardown) to quantify how many nodes each expansion above would newly cover on a target deck **before**
implementing it — the base cache was adopted only after the probe showed the reuse was real.
