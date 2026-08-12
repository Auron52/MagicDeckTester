# Mirrorwing phase-A generation: where the time goes (profile, 2026-08-12)

Profiling pass driven by the value-leaf regen plan (phase-A label runs are the wall-clock cost:
K=3 de-clairvoyed `EnumerateEarliestWins` full searches per real pre-combat main). Data below is
the Mirrorwing label config (`MTG_DUMP_VALUE_ROWS` + `MTG_EVAL_ROWS_K=3`, shipped play), current
engine (post tap-backtrack sibling collapse + mana-cache extensions, which already removed the
old payment blow-up class).

## Method notes (hard-won)

- **Callgrind DIVERTS this config's games** (a repro that wins t5 natively goes unwon under
  callgrind, both build/Release and build/Profile — natively both reproduce exactly). Cause not
  yet isolated (something wall-clock- or environment-sensitive in the value_play path). Do NOT
  trust callgrind attribution for label-config games; suite configs (explicit `--depth
  --budget-ms`) have profiled faithfully in the past.
- **perf record is broken in this WSL2 env** ("Bad address" even on `sleep`).
- What works: `build-instr` (`cmake -B build-instr -DMTG_PROFILE=ON -DCMAKE_BUILD_TYPE=Release`)
  deterministic counters, and repeated gdb stack sampling of a native heavy game. Wall A/Bs need
  `taskset` pinning (unpinned runs on this box are bimodal ±50%) and a worktree control build.
- Repro of a batch slow-game: `--seed <base+gi> --game-index <gi> --games 1` + the SAME env; the
  engine's SLOW-GAME line's args are correct.

## The shape (MTG_PROFILE counters, 60 games, seeds 900000+)

- 96% of search nodes at game turns 6–8 (fan-out boards); **90% of nodes at remaining depth 1**.
- Mean branching 31.9 candidates/node; 10.8M EnumeratePlans calls → 113M plans → 114.8M
  ApplyPlanDirect; 12.8M GameState deep copies (2.14/node); FSLine no-win memo hits 69%.
- The tail is extreme: 1 game of 60 took 951s of the 952s batch wall (16 min on one worker).

## Adopted this pass (byte-identical; pinned heavy-game A/B −4.9% combined)

1. `CardDatabase::LookupInterned` — canonical-interned-pointer → def index; kills the string
   hash+equals per fresh token Card (fan-out applies recreate tokens every speculative apply).
2. `Action::card_name/tutor_target/chosen_float_color` → `InternedName` — Actions are copied,
   sorted, destroyed ~10/plan × 113M plans; names over SSO (17-char card names) heap-allocated
   on EVERY copy.
3. `RevealLogPause` no-op fast path — nested pauses (per-EnumeratePlans, millions) found all 26
   hook globals already null and still did ~104 loads+stores; now one null-check pass.

Suite-level: neutral (smoke 40s both arms; suite wall is search-dominated there too).

## Deferred levers, ranked (from gdb samples of the real heavy game)

1. **GameState deep copy (~26% of samples; 12.8M copies/batch).** The real fix is apply/undo at
   the FSLine d1 leaves (the backtracker's pattern) or a pooled/arena Permanent storage. Big,
   behavioral-risk-free in principle but architecturally invasive; needs its own session.
2. **d1-leaf dominance (90% of nodes).** A d1 node enumerates ~10 plans, deep-copies + applies
   each, and simulates the turn end. Any structural saving here (plan-enumeration memo keyed by
   BuildSimKey, or a slimmer d1-only evaluate path) multiplies across 10M nodes. Behavioral risk:
   must stay byte-identical; the FSLine memo already absorbs 69% of revisits.
3. **BuildSimKey (~8% Ir).** O(state) fold per memo probe (11.3M/batch); incremental keying would
   be a large change — only worth it after (1)/(2).
4. **CleanupDiscardRanking (~6% Ir).** Runs only when a rollout hand exceeds 7 (real on
   Mirrorwing: Fists draws + staged cards), re-ranks per shed with per-compare
   LookupCached+ManaValue. Cache the rank per (hand multiset) or hoist MVs before the sort.
5. The label K=3 cost itself is irreducible per design (independent reshuffled futures are what
   de-clairvoys the label); `MTG_VALUE_LABEL_BNB` + the label ladder are already on.
