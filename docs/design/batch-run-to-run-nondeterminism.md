# Batch run-to-run nondeterminism — ROOT-CAUSED (2026-10-14); first reproduced 2026-09-03

**Status (2026-10-15): carriers found and fixed on branch `nondet`; every single-threaded / light check
passes (see "Validation"); the pooled 24-thread run-twice check is the one thing still owed.** The user, on the pooled-run flicker having been explained
away as "a known effect of running games in a shared pool": *"This is not something we should just
accept."* The contract this section establishes and the fix enforces:

> **A game's play digest AND its work units are a pure function of (binary, game spec)** — identical
> to what that game produces alone in a fresh process, whatever ran before it on its worker thread,
> at any thread count. The one stated exception is a binding *global* memory pool, which is now
> reported at exit rather than silently tolerated.

Everything from "## The observation" down is the 2026-09-03 investigation, kept as history. Read
this section first; it supersedes that one's open questions.

## ROOT-CAUSED (2026-10-14)

### A deterministic repro that needs no pool

The 09-03 work could only see the defect statistically (two 24-thread runs, count diverging games).
The decisive move is to make thread history CONTROLLED instead: a `--threads 1` batch whose manifest
places chosen predecessor games (the per-job `"weight"` field orders the queue) before a target game,
compared with the target run alone in a fresh process. Everything below is single-threaded, so every
number reproduces exactly.

Target: `kittyv2_regression_d5_s2002` gi=34 — the game whose digest moved in a 24-thread regression
pool on 2026-10-14 (KittyEquipment v2, depth from the profile = 5, `budget_ms` 20). A one-game chunk is
`"seed": 2002+34, "game_index": 34, "games": 1` — **not** `"seed": 2002, "game_index": 34`, which plays
game 0's shuffle under game 34's spawn index (an earlier repro attempt did exactly that and "isolated"
the wrong game).

| what ran on the thread before gi=34 (binary: base `a1ae1e67`) | gi=34 units | gi=34 digest |
|---|---|---|
| nothing (fresh process) | **48,188** | `0e31c1ce3b739553` |
| gi 0–33, same job (the GT/`--threads 1` order) | **45,409** | `0e31c1ce3b739553` |
| gi 0–33 as a *separate job* (new `AIEngine`, same thread) | 45,409 | `0e31c1ce3b739553` |
| gi 17–33, same job | 48,188 | `0e31c1ce3b739553` |
| gi 0–16, same job (17–33 skipped) | 48,188 | `0e31c1ce3b739553` |

So: the carrier is **thread_local**, not `AIEngine` (row 3 rebuilds the engine and still carries it),
and it is **cumulative** — neither half of the history moves it, the whole history does, which is the
signature of a capped cache's fill level deciding when its wipe fires. The 45,409 matches the
committed GT job (`ccf53277e7e92a6b`, 605,944 units), which was therefore recorded under one particular
history, not the game's own result. In the 24-thread pool the same game got a different history again,
and there the units moved far enough to flip the start gate and the digest.

### Carrier 1 (MEASURED): the breakpoint-enum plan cache

Bisected with existing levers, each run as the pair (predecessors 0–33, then gi=34) vs (gi=34 alone):

| lever | with predecessors | alone | history-independent? |
|---|---|---|---|
| shipped | 45,409 / `0e31…` | 48,188 / `0e31…` | **no** |
| `MTG_MANA_CACHE=0` | 45,409 | 48,188 | no |
| `MTG_NO_BP_ENUM_CACHE=1` | 58,273 / `5f71…` | 58,273 / `5f71…` | yes |
| `MTG_BP_ENUM_EPOCH=1` | 58,187 / `5f71…` | 58,187 / `5f71…` | yes |
| the 09-03 memo-class OFF set (includes the two above) | 58,273 / `5f71…` | 58,273 / `5f71…` | yes |

`BpEnumEntryFor`'s cache (`MTG_BP_ENUM_CACHE_CAP`, 8,192) is keyed on `BuildBreakpointKey` alone, is
NOT decision-scoped, and was cleared only on overflow (or a plan-pool byte refusal). It therefore
carried CONTENT and FILL LEVEL from game to game. A hit saves the derivation's units, so its history
moved units, and units reach play through the start gate. (Its served lists differ from a fresh
derivation only in a tiny residue -- `MTG_BP_ENUM_VERIFY`'s CONTENT-DIFF, 0.0079% of hits once the
benign source-only/order-only classes were split out; see BpEnumVerify in TurnSolver.cpp.)

**Fix 1:** the cache moves to namespace scope (`bpcache::t_plans`) and `TurnSolver::ClearPerGameCaches`
empties it (releasing its plan-pool bytes) at every game start. Within-game reuse across decisions is
kept. Measured on gi=34: with predecessors **48,188 / `0e31…`**, alone **48,188 / `0e31…`** — the same,
and equal to the fresh-process answer.

This is the change the 09-03 pass tried and reverted (it measured no improvement on EDF at 24 threads).
That reversion was right for EDF on that day — EDF had a second carrier, see "the 09-03 residual" below
— but wrong as a conclusion about this cache, which is a carrier on its own.

### The history-independence battery (the instrument)

`logs/nondet/h/` (gitignored; regenerate with `gen.py` / `gen2.py`, run with `hrun2.sh <bin> <out> <iso:0|1>
<seqs…>`, compare with `hcmp.py <out>`). 84 target games — 4 random games from each of 21 regression
cases across 21 decks (d3 and d5; kittyv2 d5 gi=34 included as the positive control) — each run ALONE
in its own process, then inside seven single-threaded sequences: three shuffles of the 84 as one-game
jobs (A/B/C), one with each case's games in one multi-game job (D: `AIEngine` reuse), and three heavy
pool-like histories (H1/H2: 350 games of 14 other cases first; H3: every target preceded by 6 games of
a random case, 588 games in all). ≤4 processes, `MTG_MEM_BUDGET_MB=3000`.

| binary | games differing from their fresh-process result (units / digest), all 7 sequences |
|---|---|
| base `a1ae1e67` | **2 / 0** — kittyv2 d5 gi=34 in H1 (49,244 vs 48,188), kittyv2 d5 gi=25 in H3 (49,203 vs 47,233) |
| fix 1 (bp cache cleared per game) | **0 / 0** of 588 comparisons |

The fresh-process results themselves are identical between base and fix 1 (84/84) — by construction,
since a fresh process starts with an empty cache either way. **That is the point of the fix: it makes
every game in every pool equal its fresh-process answer, and leaves the fresh-process answer alone.**

### Carriers 2–4, by audit (in the same commit; see "Validation")

Every `thread_local` in `src/` was classified by the question the user posed: *can a hit cost
different units than a miss, or can its content differ by history?* Scratch buffers (cleared or
overwritten before every read), RAII-scoped flags, diagnostic counters and the decision-epoch-scoped
memos (`solvememo`, `enummemo`, `m1memo`, the equip route's `DSeen`, `TrialPolicy`'s `DecisionMemo`)
pass. Four further cross-game channels do not, and are now emptied in `ClearPerGameCaches`:

1. **The EDF combo route's trial memo** (`comboroute::TrialMemo`, DecisionProviders.cpp). Board-keyed,
   NOT decision-scoped (both of its siblings are), key reads only the top 8 library cards, wiped only
   at 65,536 entries. → `EdfComboRouteClearMemo()`.
2. **The value-leaf hybrid's learned cost model** (`g_emul_R/Rd/G`, `g_single_R`). Meant to be per
   game — each is reset when a key of deck + arm + **game seed** changes — but a key-change test cannot
   see a new game whose key equals the last one's, and every suite deck runs its d3 and d5 jobs on the
   SAME seeds (`_d3_s2002` game k and `_d5_s2002` game k are both game seed 2002+k). Emptying the keys
   at game start makes "per game" literal. `g_esc_R` (adaptive only on the `MTG_ESC_PREDICT` /
   `MTG_ESC_SINGLE` env research paths) persisted across games outright; it now starts each game at its
   documented "uninitialized" 0. → `HybridLearnedStateClear()`.
3. **The payable-mana cache** (`g_mana_cache`, SpellEffects.cpp). Pure by design, so where its key is
   complete a per-game clear changes nothing but wall clock (payments are unmetered). But every key
   hole it has had — the `MTG_FILTER_FEED_STRICT` lever, the 2HG heads delta, the scaled-dork yield,
   **and EDF's land-Aura hole, found 2026-09-07, four days after the 09-03 runs** — was a cross-game
   channel precisely because the cache outlives the game. Cleared per game, a hole still gives a wrong
   answer, but the same one every time: reproducible and caught by GT / `MTG_MANA_CACHE_VERIFY`
   instead of surfacing as pool flicker. `MTG_LEGACY_MANA_CACHE_ACROSS_GAMES=1` restores retention
   (the A/B hatch for its wall cost). A game whose result moves with this flag has found the next hole.

**The 09-03 residual, explained.** The 09-03 "memo class OFF" experiment was never written up; its
data (`/workspaces/MagicDeckTester2/logs/edf_memo_off/`, EDF s4303, 100 games x2 at 24 threads,
per-game keyed): ON 2/100 games unit-divergent (gi 34, 95), OFF 2/100 (gi 57, 85), 0 digest changes
in either. The OFF set disabled the bp cache and every plan memo but NOT the mana cache — and on that
date EDF's mana-cache key had the land-Aura hole. That is the most likely reason clearing the bp
caches alone "did not work" on EDF. (Not proven: the EDF battery that would test it on today's binary
was stopped when the box was needed for a value-leaf generation, and is listed below.)

### The residual: the two GLOBAL memory pools (detected, not hidden)

The plan-cache byte pool (`MTG_PLAN_CACHE_KB`, default workers x 100 MB) and the FSL line-cache pool
(`MTG_FSL_POOL`) are process-wide by user-approved design (2026-09-11: one monster enumeration may
draw GBs while typical workers hold little). Their contract — "a refused store just recomputes, so
play is identical at any budget" — is true of content but not of WORK: a recompute can cost units
(the bp cache's derivations do, see the table above), and a bp-cache byte refusal wipes the cache. With
every per-game cache now reset, one thread's pool use is a function of its own game, but under a
multi-threaded batch a refusal depends on what OTHER workers hold. That cannot be made per-game
deterministic without giving up the shared pool. So it is surfaced instead: both pools now count
refusals, and a `[determinism] WARNING` line prints at exit iff any store was refused.

Measured headroom in the last regression-tier pool heartbeat (2026-10-13, 24 threads, primary
checkout): plan pool hiwater **1,497 MB of 2,400 MB**, FSL **52 MB of 7,234 MB** -- neither binds, and
the plan pool's use should fall further (unmeasured) now that the bp cache no longer accumulates for a worker's whole
life. If the warning ever fires on a GT tier, the choice between per-thread pool shares and more
memory is the user's.

(Cross-MACHINE identity is a separate axis, not addressed here: the MemBudget-derived per-table TT cap
scales with RAM and CPU count, so a decision that fills its table could differ on a different box.)

### Cross-decision reuse in the bp cache changes play — a USER decision, not taken

Played alone in a fresh process, gi=34 gives `0e31c1ce3b739553` at 48,188 units with the shipped
cache, and `5f7114a3f7298e48` (same win turn T6) under both `MTG_BP_ENUM_EPOCH=1` (58,187 units) and
`MTG_NO_BP_ENUM_CACHE=1` (58,273). So the cache's reuse ACROSS DECISIONS within one game moves play.
The LIBCHECK measurement below settles why: on this deck every earlier-decision hit served exactly the
list a fresh derivation produces, so the move is the units the reuse saves, not unsound content. The
per-game clear removes only the cross-GAME channel and does not touch this. Epoch-scoping would cost
those units for no soundness gain on the evidence; it stays default-off, and whether to change it is
the user's call.

### What the fix does to budgeted play and GT

Every game's result becomes its fresh-process result. GT was recorded in pools, so any GT game whose
recorded play came from a contaminated history moves; nothing else can. Expected size is small — in
the battery, pool-like histories on the base binary moved units on 2 of 84 sampled games and digests on
none, and the regression tier reported one changed digest. A concrete prediction to check:
`kittyv2_regression_d5_s2002` (`--threads 1`) keeps digest `ccf53277e7e92a6b` (every per-game digest
already equals its fresh-process digest) and its units go from 605,944 to **608,723** (gi=34
45,409 → 48,188; the other 49 games already match their fresh-process units). **No GT is accepted or
edited by this change** — the pooled validation below sizes it for the user.

### Is the bp cache's reuse UNSOUND? Measured: not through the library (`MTG_BP_ENUM_LIBCHECK`)

The user's direction: *"If that cache reuse is truly unsound then we will have to try out a sound
version and see if it fixes things."* The key already folds an ORDER-EXACT digest of both libraries by
card NAME (BuildSimKey, since 2026-08-20); what it does not fold is the PHYSICAL copy order (card
numbers). `MTG_BP_ENUM_LIBCHECK=1` (diagnostic, default off) stamps every entry with ordered library
hashes by card number and by name plus a per-thread game ordinal, and on every hit re-derives the list
fresh and tabulates the result (exit report `=== BP ENUM LIBCHECK`). Run on the full fix binary,
`--threads 1`, the regression cases' own seeds:

| deck (case, games) | hits re-derived | library differs (copy / name) | cross-game | CONTENT differs: this decision / earlier decision | source-only |
|---|---|---|---|---|---|
| FiveColour d3 s2002 x100 (fetches) | 162,088 | 0 / 0 | 0 | 0 / 0 | 0 |
| Melira Pod d3 s2002 x75 (tutors) | 378,684 | 0 / 0 | 0 | 88 / 70 | 138 |
| Dragonstorm d5 s2002 x150 (tutors) | 82,557 | 0 / 0 | 0 | 0 / 0 | 0 |
| KittyEquipment v2 d5 s2002 x50 | 142,808 | 0 / 0 | 0 | 43 / 0 | 0 |
| **total** | **766,137** | **0 / 0** | **0** | **131 / 70** | 138 |

**Reading.**
1. **The library is not a soundness gap.** Not one hit in 766,137 was served under a different library
   — not by name, not by physical copy — on the two shuffling decks included. So the sound version the
   user asked for, "fold a shuffle counter or library hash into the key", has nothing to fix; it was
   built behind a flag (`MTG_BP_ENUM_LIBKEY`) for exactly this decision and has been removed unmeasured
   rather than left as a dead lever.
2. **On KittyEquipment the cross-decision reuse is content-SOUND:** 54,845 earlier-decision hits, zero
   with different content. So gi=34's play move under `MTG_BP_ENUM_EPOCH=1` (`0e31…` → `5f71…`) is the
   ~10k units the reuse saves the start gate, not a wrong list served. Epoch-scoping would make the
   search pay again for the same lists.
3. **A small residue of CONTENT differences remains, and it is not about the library or about
   staleness across decisions:** 201 of 766,137 hits (0.026%), every one with the SAME library, 131 of
   them inside ONE decision (which epoch-scoping could not remove either). KittyEquipment's 43 are all
   at a Skateboard breakpoint with `served=1 fresh=2`; Melira's are equal-length lists with different
   content. That is the same residue BpEnumVerify recorded on Snow (served a subset of fresh), and it
   points at an unkeyed input that changes WITHIN a decision — the equip route's budget-priced trial is
   the obvious suspect for the Skateboard cases. It is deterministic per game after this fix, so it is
   a search-completeness question, not a determinism one; naming the differing plans with
   `MTG_BP_ENUM_VERIFY=2` on these two cases is the next step if the user wants it pursued.

(Caveat on the instrument: re-deriving on every hit spends extra units, so the instrumented runs' play
is not the shipped play — kittyv2's digest moved to `ca604b4e…`. The classification of each hit is
still exact; the counts describe a nearby trajectory, not the GT one.)

### Validation (2026-10-15)

All on the full fix (bp cache + combo-route memo + hybrid keys + mana cache + pool report; LIBCHECK
present but off), binaries copied to the ext4 build volume (`build/wt/nondet/`) after the 9p mount died
once under a running batch. Cost is never read from wall or user time here: the VM was shared at load
45–90 on 24 cores, and its PMU is not exposed (`perf stat -e instructions:u` reports "not supported"),
so cost comes from the engine's own deterministic counters.

1. **Build + unit tests:** clean; `build/Release/mtg-test` **596/596 test cases, 2,646,986 assertions**
   (on the probe binary and again on the final source, `build/wt/nondet/mtg.fix3`, which differs only by
   the removed `MTG_BP_ENUM_LIBKEY` read and reproduces the kittyv2 job game-for-game: 50/50 identical).
2. **gi=34:** with predecessors 0–33 **48,188 / `0e31c1ce3b739553`**, alone **48,188 / `0e31…`**.
3. **The kittyv2 prediction holds exactly:** `kittyv2_regression_d5_s2002` at `--threads 1` gives digest
   **`ccf53277e7e92a6b`** (= GT) and **608,723** units (GT run: 605,944), and all 50 games equal their
   fresh-process results (base: 1 exception, gi=34).
4. **History battery** (84 games x 7 sequences, see above): **0 units / 0 digest differences in 588
   comparisons** (base: 2 / 0), and the 84 fresh-process results are identical to base's.
5. **EDF**, the original 09-03 deck (d5/b20 s4303; seqE1 = games 0–39 as one job, seqE2 = 60 games of
   kittyv2 d5 / Fungus d5 / Hinata d3 / Melira d3 / EDF s5505 then targets 24–39 as one-game jobs):
   **0 / 0 differences in 32 comparisons on BOTH binaries**, and base and fix2 agree on all 132 game
   results (16 fresh-process, 40 + 76 in sequence). So today's EDF shows no history effect at this
   power — consistent with its 09-03 residual having been the mana-cache land-Aura hole fixed on 09-07
   — and the fix changes nothing on it.
6. **Mana-cache per-game clear, same binary, `MTG_LEGACY_MANA_CACHE_ACROSS_GAMES=1` vs unset**, the
   smoke manifest capped at 20 games per job (125 jobs, 2,490 games incl. every 2HG job), both arms
   concurrently at `--threads 2`, `MTG_TAP_STATS=1`:
   * **identity: 2,490 / 2,490 games identical in digest, win turn AND units** — no mana-cache key hole
     surfaced, and payment really is unmetered;
   * **cost:** cache misses 839,885 → 954,079 (+114,194, hit rate 92.3% → 91.3%), backtracker nodes on
     misses 4,091,073 → 4,689,134 (+598k, ~240 per game), against 41.6M payment calls that are the same
     in both arms. An instruction count could not be taken (no PMU); at any plausible 1–10 µs per
     backtracker node that is ~0.6–6 s over a ~0.8 core-hour arm, i.e. well under 1%. A callgrind spot
     check would make it exact if wanted.
   * no `[determinism]` line in either arm (neither memory pool refused a store at `--threads 2`).
7. **Soundness count:** see the LIBCHECK table above — zero library-mismatched hits; `MTG_BP_ENUM_LIBKEY`
   removed; a 0.026% same-library CONTENT residue left as a separate (non-determinism) question.

Owed, needs the whole box (the coordinator's run):

8. **Pooled:** the regression manifest twice at 24 threads and once at a different thread count, each
   `MTG_DUMP_UNITS=1 /workspaces/MagicDeckTester2/build/wt/nondet/mtg.fix3 --batch <manifest> --threads N --game-log-dir
   logs/nondet/pool/<run> > logs/nondet/pool/<run>.out 2> logs/nondet/pool/<run>.err` from the `nondet`
   worktree root, compared with `python3 logs/nondet/poolcmp.py logs/nondet/pool/<A> logs/nondet/pool/<B>
   --gt test/gt_logs`. Pass = 0 digest and 0 units differences between every pair and no `[determinism]`
   line in any `.err`; the `--gt` report sizes the GT change.

## History: the 2026-09-03 investigation

## The observation

Same binary, same manifest, run twice back to back. No code change, no rebuild, nothing else on the
box. `logs/edf_tapyield/out.run1.txt` vs `out.run2.txt`:

| job | avg r1 | avg r2 | digest r1 | digest r2 | verdict |
|---|---|---|---|---|---|
| off_s4101 | 7.07 | 7.07 | `1ad5deba38ddb12e` | `1ad5deba38ddb12e` | stable |
| off_s4202 | 7.07 | 7.07 | `09beee6f982df55e` | `0f8cb9712226171f` | **diverged** |
| off_s4303 | 7.09 | 7.09 | `2e730b016fbdbf4a` | `5f38514adc38a918` | **diverged** |
| off_s4404 | 7.07 | 7.07 | `46ea3a80199f7a14` | `27d61c435fc30a1a` | **diverged** |
| on_s4101 | 7.07 | 7.07 | `9d4da40162b007a3` | `9d4da40162b007a3` | stable |
| on_s4202 | 7.07 | 7.07 | `8eaf798d77bc2d75` | `41ceaec2ae505230` | **diverged** |
| on_s4303 | 7.09 | 7.09 | `3e0242dcb250f1b5` | `7e055a89fcc3b7ee` | **diverged** |
| on_s4404 | **7.06** | **7.07** | `e4199309b1e9be64` | `899f231c42bcd18d` | **diverged, avg moved** |

**6 of 8 jobs diverged in play; 1 of 8 moved its average** (by 0.01 — one game's win turn shifted by
one turn). Both arms of an A/B diverge, so this is not a property of the lever under test.

Seed 4101 is stable in *both* arms while the other three seeds are not, so the divergence is
seed-dependent rather than uniform — which differs from the 2026-08-25 sighting (uniform across
game indices, all faster) and may or may not be the same underlying cause.

## Why this matters more than its size

**A play digest is not a valid byte-identity check on this deck.** Anything that concluded "the
digests changed, so the change had an effect" — or "the digests match, so the change is inert" — was
reading apparatus noise. That is a live hazard across the repo, because digest-identity is the
standard byte-identity argument (smoke/regression "0 configs changed", the A/B off-arm control).

Two consumers to re-examine:
* **The off-arm-as-control pattern.** Running a default-off lever's OFF arm and comparing its digest
  to a previous run is the natural way to prove a lever inert. On this deck it proves nothing.
* **Averages are far more robust than digests, but not exempt.** The measured noise floor here is
  ~0.01 turns on a 100-game job. Any effect at or below that needs a repeat-run bracket before it
  means anything — the same discipline `deck-screening.md` applies to the apparatus bias floor.

The immediate consequence for this deck: the −0.135-turn mana-fix result (`analysis-Eldrazi
DisplacerFlicker.md`) stands, because it is 10x the noise floor and consistent across four seeds at
three depths — but its "all twelve digests changed" line was withdrawn as evidence.

## Reproduction

```
mtg --batch logs/edf_tapyield/manifest.json > run1.txt
mtg --batch logs/edf_tapyield/manifest.json > run2.txt
diff <(grep avg run1.txt | sort) <(grep avg run2.txt | sort)
```

8 jobs x 100 games, EldraziDisplacerFlicker at d5/b20, ~13 min on 24 cores. Seeds 4101/4202/4303/4404.

## Already refuted in the 2026-08-25 investigation (do not re-derive)

A binary difference (all copies md5-identical), a missing keep model, the rollout bottomer
(`MTG_BOTTOM_ROLLOUTS=0` was digest-identical), the `.bincache`, a sibling value model, a load-time
race (differences were uniform, not clustered early), and a plain concurrency race (48 concurrent
copies of one job gave 48/48 identical digests).

## NARROWING (2026-09-03, this session) — it needs the POOL, and it looks like budget, not play

Three experiments, each the same job (`off_s4303`, EDF d5/b20, 100 games, seed 4303):

| pool | runs | s4303 digest | jobs diverged |
|---|---|---|---|
| 1 job, 100 games (`logs/edf_nondet/`) | 3 | `2e730b01…` all three | 0 of 1 |
| 4 jobs, ONE config, 4 seeds (`logs/edf_samearm/`) | 2 | `18b562c3…` then `1630f11f…` | 1 of 4 |
| 8 jobs, two arms (`logs/edf_tapyield/`) | 2 | `2e730b01…` then `5f38514a…` | 6 of 8 |

> **RETRACTED: "it needs the pool" and "the rate scales with pool pressure".** Both were read off
> this table, and the table is UNDERPOWERED. The real unit of divergence is the GAME, and the rate
> is **~1–2% of games**; a job-level digest diverges iff at least one of its games does, so a
> 100-game job has only a ~1−e^−1.5 ≈ 78% chance of showing it at all and three identical runs is
> an unremarkable coincidence (~25%), not evidence of stability.
>
> **Measured properly: a SINGLE job of 400 games, run twice at 24 threads, diverges 8/400 (2.0%)**
> (`logs/edf_onejob/`, 2 of the 8 changed a win turn). One job is enough. Job count, mixed arms and
> mixed depths are all irrelevant — they only ever changed how many games were in the sample.
>
> This is a good illustration of the trap: every row above is *true*, and the conclusion drawn from
> them was still wrong, because "did this job's digest change" is a coarse, sample-size-dependent
> proxy for "did a game change". Count the games.

Seed 4101 never diverged in any run, and its digest `1ad5deba38ddb12e` is identical across every run
*and* across the pre-fix binary, which makes it a usable byte-identity control — with the caveat
above that "never diverged" over a handful of runs is weak evidence for any single job.

**A job run ALONE is deterministic; the same job inside a pool is not.** That kills the simplest
stories (a wall-clock read in the decision path would diverge the solo run too) and points at state
that a worker thread carries ACROSS games — the pool's interleave decides which games precede which,
and that interleave is not reproducible.

**And there is a strong hint about the mechanism.** In the single-config pool, the d5 job for seed
4202 produced digest `cdc8f27a9d0d8b9d` — which is *exactly* the digest the **d3** job produced for
that seed in the re-measurement. A d5 run landing precisely on the d3 play is not a coincidence at
32 bits of prefix; it means **the depth actually reached varied**, which is the known starvation
story (this deck's first iterative-deepening pass can consume the whole decision budget, so d3 and
d5 collapse to the same computation — see `analysis-EldraziDisplacerFlicker.md`).

That yields a concrete, testable mechanism:

> The budget is denominated in deterministic work units, but a CACHE HIT COSTS FEWER UNITS THAN A
> MISS. A cache that survives across games on a worker thread therefore makes the units consumed by
> a given search depend on which games ran before it — so at the starvation edge, where one extra
> ID pass either fits in the budget or does not, the depth reached flips with the interleave. The
> budget is deterministic; the *work* priced against it is not.

This is the same class as the already-fixed defect where the m2 solve memo's dead cross-game entries
shifted cap-clear timing and broke work-unit thread-invariance. That fix covered one cache; this
would be a second one still carrying state across games.

It also explains the seed-dependence: seed 4101 is stable everywhere because its games are not
sitting on the budget boundary, while 4202/4303/4404 are.

**Consequence if confirmed: the effect is concentrated on decks at the starvation edge**, which is
where the budget boundary is close enough for cache-warmth to tip it. That is a much narrower blast
radius than "all measurements are unreliable" — but EDF is squarely in it, and any deck whose
`id_depth` histogram is not saturated may be.

**Corroborating evidence for exactly that narrowness:** the smoke tier — 48 configs, itself a pooled
batch — reports `configs changed: 0` run after run, and the regression tier likewise. So the shipped
suites are NOT visibly affected, which is why this survived so long: the harness that would have
caught it is made of decks that are not on the boundary. Do not read "smoke is green" as "the engine
is deterministic"; read it as "no smoke deck is starved".

### CONVERGENT EVIDENCE, found independently the same day (46e8efb4)

Another agent's recoverability audit landed a fix whose description is the mechanism class above,
arrived at from a different direction:

> `BpEnumBuildKey` folds the whole heurarm `t_arm`: the bp-enum plan cache + canon verdict memo are
> **thread_local, survive batch job switches, are NOT in `ClearPerGameCaches`** — a MIXED-ARM pooled
> batch shared enumeration entries across arms whenever a lever changes enumeration output (the
> batch-pool-contamination class, second instance).

That is exactly "a cache that survives across games on a worker thread", confirmed and fixed for the
**mixed-arm** case. It very likely explains the 8-job (two-arm) result here — 6 of 8 diverging.

**It does NOT explain the single-config result, and I re-ran that case ON the fixed binary to be
sure.** Keying the cache by the heuristic arm cannot help a pool whose jobs all share one arm, and
it doesn't:

| single-config 4-job pool, run twice | diverged | averages moved |
|---|---|---|
| before the arm-key fold | 1 of 4 | 0 |
| **after the arm-key fold** | **3 of 4** | **2** (6.98→6.97, 6.93→6.94) |

So the residual is not only real, it is the *larger* half on this deck, and post-fix it moves
AVERAGES rather than just digests. (The two rows are not a controlled comparison of the fix — the
same commit also changed play via the B&B aura bound — so read them as "still broken, at least as
badly", not as "the fix made it worse".) Seed 4101 remains stable in every run, before and after.

The caches still survive job and game switches within an arm, so cross-*game* contamination remains
even with the arm folded into the key. The open question is narrower and sharper than it was this
morning:

> Which thread_local caches survive a game boundary without being in `ClearPerGameCaches`, and is
> any of them keyed on something that does not fully determine its value?

Note also that the same commit fixed a **fifth** land-Aura blind spot (the B&B max-mana bound in
`SourceMaxNet` / `source_max_net`, which could prune a payable cost). Measured here on the same
4 seeds x 100 games: **7.068 → 6.92**, another −0.145. Three independent mana-modelling fixes landed
on this deck in one day, each worth ~0.14 turns, for a cumulative **7.206 → 6.92 (−0.29)** — every
one of them larger than any tuning lever ever tried on it. That is the strongest available evidence
for the conclusion recorded in the deck ledger: on this deck, hunt modelling gaps, not levers.

### The next test, and it is cheap
Run the pooled manifest with `MTG_ROLLOUT_STATS` and compare the `id_depth` histogram and
`units_total` across two runs. If units move run-to-run on identical games, the mechanism above is
confirmed and the hunt narrows to *which* cache is not cleared per game. If units are identical and
only play differs, this is wrong and the cause is elsewhere.

## ROOT-CAUSE PASS 2 (2026-09-03, later) — it IS concurrency, and here is what it is not

Every experiment below counts DIVERGING GAMES over 400-game runs (see the retraction above for why
job digests are the wrong unit), EldraziDisplacerFlicker seed 4303, d5/b20.

### The one solid structural fact: single-threaded is deterministic

| condition | diverged |
|---|---|
| `--threads 1`, 200 games, two independent runs | **0 / 200** |
| `--threads 24`, 400 games, two runs | **8 / 400 (2.0%)** |
| serial vs 24-thread over the same games 0–199 | 198 / 200 identical |

At the measured 2% rate, seeing zero divergence in 200 serial games has probability 0.98^200 ≈ 1.8%.
**Concurrent execution is required.** The serial answer also equals the isolated single-game answer,
so serial/isolated is the "true" result and the threaded run is the one that deviates.

### And it is NOT cross-game state carried on a worker thread

The obvious story — a thread_local cache surviving a game boundary, so a game's result depends on
which games preceded it — is *false* here. Games 47 and 67 give byte-identical digests whether run
completely alone or after 46 / 66 preceding games on the same thread:

```
isolated (game alone):  47 -> 7bb35aaf366f0679 | 67 -> 7e1b0e3ef2f9b550
serial  (after 46/66):  47 -> 7bb35aaf366f0679 | 67 -> 7e1b0e3ef2f9b550
```

So it needs threads running *at the same time*, not merely a dirty thread.

### Eliminated, each at proper power or by construction

* **The bp-enum plan cache + canon verdict memo** (the leading suspect, and the thing the arm-key
  fix touched). Fully disabled via `MTG_NO_BP_ENUM_CACHE=1`: **10/400 diverged, vs 8/400 with it on**
  — an unchanged rate. Note the earlier "identical digest at cap 8192 / 64 / off" check was itself
  underpowered (one 100-game digest per arm, the same ~25%-coincidence trap); this replaces it.
* **Data races** — a ThreadSanitizer build (`build/TSan`, deliberate explicit-build-type route)
  reported **zero** warnings across three workloads (5 concurrent games; 4 jobs x 6; 4 jobs x 40
  partial). Not conclusive for a rare race, but nothing on the hot path.
* **Uninitialised memory** — `valgrind --tool=memcheck --track-origins=yes` on a diverging game:
  **0 errors from 0 contexts**.
* **The per-game abandon ceiling** — `abandon_*` are 0 here, and `need` is a property of the
  manifest, not of completion order (the code says so and means it).
* **In-flight condemnation / `max_game_sec`** — defaults to 0 without a `condemn` block.
* **The profile LRU** (mutex-guarded, keyed on path+value-sidecar, hands out `shared_ptr<const>`),
  **the sidecar-resolution memo** (mutex-guarded — already chased for the 2026-08-26 incident),
  **`LookupCached`** (`std::atomic_ref` by design), **every instrument map** (all mutex-guarded),
  **the ID start gate** (constants plus the game's own measured growth ratio), **wall clocks in the
  decision path** (only two, both instrument-gated), and **pointer-derived hashing** (one cast, for
  a temp filename).

### Where it bites, and the one live lead

The divergence is in **committed search depth** — the `id_depth` and `committed_depth` histograms
differ between two runs of the same manifest — which is why it is confined to a small, *repeatable*
set of games: those sitting exactly on the iterative-deepening budget boundary, where one more pass
either fits or does not. Games 146, 265, 305, 317 and 367 diverged in two or more independent
conditions.

### The reporting paths MODULATE the rate but are not the cause

Bisected, one 400-game pair per condition:

| condition | diverged / 400 |
|---|---|
| baseline (both reporting paths on) | 8 |
| `MTG_NO_BP_ENUM_CACHE=1` | 10 |
| `MTG_BATCH_HEARTBEAT=0` (heartbeat thread off) | 7 |
| `MTG_SLOW_GAME_MS=0` (worker-thread stderr off) | 4 |
| both off | **2**, and **2** on a replicate |

The heartbeat *thread* accounts for none of it. What matters is the **SLOW-GAME `fprintf`+`fflush`
that WORKER threads perform** — this deck emits dozens per run, and serialising workers on stderr is
exactly the kind of timing perturbation that changes which interleavings occur.

**So these flags are an amplifier, not the mechanism, and the honest conclusion is the harder one:
the underlying nondeterminism is TIMING-SENSITIVE and survives with all reporting off (4/800).**
Neither flag can change play by construction — both are pure `fprintf` after a game has finished,
and `MTG_SLOW_GAME_MS`'s only other use is in `GoldFishRunner`, which the batch path does not use.

### Where that leaves it

Something about concurrent execution changes **how much work a game's search does**, which flips the
committed depth for games sitting exactly on the iterative-deepening budget boundary — and it is not
a data race TSan can see, not uninitialised memory, not a clock in the play path (every remaining
`steady_clock` in the tree is in `src/analyzer/`, unused by batch play), and not the known caches.

### ROOT CAUSE CHAIN, ESTABLISHED (`MTG_DUMP_UNITS=1`, 400 games x2, 24 threads)

Per-game work units are recorded by `MTG_DUMP_UNITS=1` (a `<job>.units` file beside `<job>.wins`).
Diffed across two runs of the same manifest:

* **69 / 400 games (17%) consumed a DIFFERENT number of work units.**
* **6 / 400 diverged in play — and all six are a SUBSET of the 69.**

```
game 305: units 50527 vs 87556   -> win turn 8 vs 7
game 143: units 167465 vs 153633
game 317: units  85921 vs  81671
```

So the chain is: concurrency -> a game's consumed work units vary -> the iterative-deepening start
gate (`estimated cost <= alpha * remaining budget`) sees a different remaining budget -> the
committed depth flips -> play differs. Play only moves for the minority of games sitting exactly on
the boundary, which is why the play-level rate is 2% while the underlying defect touches 17%.

**This falsifies the conclusion recorded at the `ClearPerGameCaches` fix site**, which says of the
same class of defect: *"Play was never affected; the work METER's determinism was."* The meter and
the search budget are the same currency (`SearchBudget::Consume` calls `gamework::Add`), so meter
nondeterminism reaches play by construction. That comment should be corrected when this is fixed.

**METHODOLOGY NOTE, and it is the most reusable thing here: use UNITS, not play, as the detector.**
It is 8x more sensitive (17% vs 2%), so a bisect needs ~8x fewer games for the same power. Every
underpowered mistake earlier in this document came from testing with the insensitive signal.

**REMAINING QUESTION (narrow):** why does a game's work vary at all? Its inputs are identical and it
is deterministic single-threaded. The unit sites are the 13 `ConsumeAt(budget, unitsite::...)` calls;
`MTG_ROLLOUT_STATS` prints the per-site partition but only as a PROCESS aggregate, so the next step
is a per-game site partition (or a bisect over the sites) on one fragile game such as 305.

**CONFIRMED (2026-09-03): work units are deterministic single-threaded — 0 of 50 games differ.**
50 games x2 at `--threads 1` under `MTG_DUMP_UNITS`, byte-identical `.units` files
(`logs/edf_units1/{a,b}`). At the threaded 17% rate this had P(false clean) = 0.83^50 ~ 1e-4, so it
is a high-power check, not a token one. The chain above is therefore concurrency-gated end to end:
the perturbation cannot originate inside a game's own deterministic computation, so it enters
through **state a game does not own** — either shared across threads, or inherited from whatever ran
on that thread before it.

That is worth stating precisely, because it is what makes the remaining search tractable: the
culprit must be reachable from the search AND carry information across a game boundary or a thread
boundary. Pure per-game state, however complex, is exonerated wholesale.

**(superseded) THE NEXT PROBE, and it is cheap: `MTG_DUMP_UNITS=1` records per-GAME work units.** Diff them
across two runs for a fragile game:
* units differ ⇒ the game genuinely does different work ⇒ bisect the `ConsumeAt` unit sites
  (`unitsite::` partition) to find which one varies;
* units identical but play differs ⇒ the divergence is NOT in work accounting at all, and the
  budget/depth story above is wrong — start again from the decision that differs.

Either answer is decisive, which is why it is the right next step rather than more code reading.

## LOCALIZED (2026-09-03) — it is INHERITED THREAD HISTORY, and here are the two caches

The decisive measurement cost nothing: it is a re-read of data already on disk
(`logs/edf_units/{a,b}`, 400 games, 24 threads). Bucket the unit-divergent games by game index —
which, under a pooled queue with roughly equal game costs, is dispatch order:

| games (by index) | unit-divergent |
|---|---|
| 0-23   (dispatch wave 0) | **0 / 24** |
| 24-47  (wave 1)          | **0 / 24** |
| 48-95                    | 2 / 48 |
| 96-199                   | 16 / 104 |
| 200-299                  | 21 / 100 |
| 300-399                  | 28 / 100 |

**The first two dispatch waves — the 48 games whose worker threads have no history yet — do not
diverge at all.** The rate then climbs monotonically with position in the run, to ~25%.

This explains every earlier confusion at once. The 100-game run's 2/100 is not a different rate: it
is quartile 1, reproduced independently. So the rate is not a function of thread COUNT (which was
the "needs the pool / scales with pool pressure" story, already retracted) and not a property of
concurrency as such. It is a function of **how many games have already run on that thread**. Once a
thread's carried state differs between two runs, every subsequent game on that thread is exposed.

That kills the remaining "shared state accessed concurrently" hypotheses outright: a race on shared
state would hit game 0 as readily as game 399. It has to be state a thread inherits from its own
predecessors — and, since single-threaded runs are clean (0/50), state whose *contents depend on
which predecessors ran*, which in a pool is not reproducible.

### The two caches

`ClearPerGameCaches` resets `solvememo::t_cache`, `solvememo::t_m2cache` and `enummemo::t_cache`.
Two more capped `thread_local` caches were function-local statics, unreachable from it:

* the breakpoint plan cache in `BpEnumEntryFor` (`MTG_BP_ENUM_CACHE_CAP`, default 8192), and
* canon's ACT-vs-PASS verdict memo (same cap x4).

Both are cleared **only on overflow** (`if (cache.size() >= cap) cache.clear()`), so a game inherits
its predecessors' FILL LEVEL, the cap-clear lands at a different point, and the hit/miss pattern
moves. A hit returns without calling `EnumeratePlansWithLand`; a miss pays it. Different work, same
game — which is the whole chain's first link.

### And it is not only a determinism problem

The three caches that ARE cleared are also **epoch-scoped**: every hit demands
`entry.epoch == g_decision_epoch`. That check is load-bearing, and the codebase says why, in
`solvememo`'s own doc block: the key's library digest is *size + front card*, and that
"only implies content within a single decision, the TranspositionTable's own scoping argument."

The two breakpoint caches use the same `BuildBreakpointKey` and do **not** fold or check the epoch —
they hit on the key alone. Within one game that is defensible: the library is a fixed permutation,
so remaining size does imply remaining content. **Across games it is not** — two different shuffles
sharing a library size and front card share a key, and the second game gets the first game's plans.
Clearing per game closes that cross-game path. It does NOT close the cross-decision path within a
game, where the residual exposure is a mid-game shuffle (a fetchland, a tutor-and-shuffle) changing
the permutation while the cache still holds pre-shuffle entries. FiveColour is the deck to check
that on; EDF barely shuffles. **Flagged, not fixed, and not yet measured.**

### The attempted fix — MEASURED, IT DOES NOT WORK, AND IT WAS REVERTED

Hoisting both caches to namespace scope (`bpcache::t_plans`, `bpcache::t_verdicts`) and clearing
them in `ClearPerGameCaches` **does not remove the divergence.** 400 games x2 at 24 threads, the
same conditions as the 69/400 control:

| binary | unit-divergent | wave 0 (gi 0-47) |
|---|---|---|
| unfixed (control) | 69 / 400 | 0 / 48 |
| both caches cleared per game | **102 / 400** | 1 / 48 |

The rate did not fall, and — the part that actually settles it — **the dispatch-wave signature
survives unchanged**: still ~0 in the first wave, still climbing monotonically with position in the
run. So thread history is still being carried. These two caches were carriers, but not the one that
matters.

**Reverted**, because the change is not byte-identical (that was expected and is inherent), so it
would have cost a full GT rebaseline while delivering no measured determinism benefit. Adopting a
correctness-motivated change that shows no measured gain is exactly the trap
`measure-before-fixing-a-shipped-bug` records. The revert is a `git checkout` of the file to the
pre-fix commit and `git diff` against it is empty, so source identity is the proof; no behavioural
re-run was needed or done.

### What the two refuted guesses buy: a model of the whole thing

Both remaining questions are answered by one more free re-read — whether the SAME games diverge in
the fixed and unfixed runs:

| | games |
|---|---|
| unfixed divergent set | 69 |
| fixed divergent set | 102 |
| overlap | **39** |
| expected overlap if position-driven only (independent) | 17.6 |
| max possible overlap if identity-driven only | 69 |

39 is far above chance and far below identity, so **both factors are real and they multiply**:

* **SUSCEPTIBILITY is a property of the game.** A game whose decision sits near the ID start gate
  flips on an arbitrarily small change in work. Roughly twice as many games are susceptible as
  actually flip in any one pair of runs.
* **PERTURBATION is a property of run position.** It comes from thread-carried state and grows with
  how many predecessors the thread has seen.

Divergence = susceptibility x perturbation. That is why the deck matters (starved decks are
susceptible, smoke decks are not), why position matters, and why killing one carrier of the
perturbation moved the total hardly at all — there is at least one more, and the susceptible
population is unchanged.

**It also means there are two independent places to fix this**, and the second may be the better
one: reduce susceptibility by making the start gate insensitive to small work deltas (a hysteresis
band, or committing depth on a quantised budget), rather than chasing every carrier of a
perturbation that will never be provably zero. That is a design change and is NOT proposed here
without measurement, but it is the more robust direction and it should be on the table before
anyone else spends a day hunting carriers.

### Audited and NOT the carrier (2026-09-03)

* **`AIEngine` members.** Worker threads reuse `AIEngine` across games, so its members are a bounded
  carrier vector, and the game-start block resets most of them. Two gaps found, neither the cause:
  `m_fd_best_leafest` is not reset (harmless in the ship configuration — it is written only under
  `MTG_FD_ORACLE` and is diagnostic), and `m_ext_main_ordinal` is deliberately cumulative (a viewer
  side-channel key, not read in batch play). `m_nonconv_best_win` IS reset.
* **`g_decision_epoch`'s VALUE.** It is thread_local and monotonic and never reset per game, so it
  does differ between runs for the same game. But it is only ever compared for equality, and the one
  place its value enters a hash (`k.h1 ^= g_decision_epoch * ...`) is inside `CONSIDER_STATS`, a
  gated diagnostic.
* **`SearchBudget`.** No carried state at all — the growth constants are `constexpr`.
* **`NameRegistry` / `SubtypeRegistry`.** Safe by construction, argued above.

**The soundness hazard is real but UNMEASURED, and is left as an open item rather than fixed.**
These two caches genuinely do hit on `BuildBreakpointKey` alone with no epoch check, in a regime
where the codebase's own scoping argument says the key's library digest does not imply content. What
is missing is any evidence that a cross-game hit ever actually occurs. The cheap instrument: stamp
each entry with a monotonic game counter on insert and count hits whose stamp is from an earlier
game. If that counter is zero on a few hundred games, the hazard is theoretical and can be closed
with a comment; if it is non-zero, it is a correctness bug independent of determinism and worth its
own fix and its own measurement. **Do not re-adopt the per-game clear without that number** — this
document is the record that clearing alone buys nothing.

### One methodology note, because it bit this very comparison

Counting divergences with `diff a.units b.units | grep -c '^<'` is **wrong** and under-reports:
`diff` is free to pair up non-corresponding lines, so it reported 93 where the correct
game-index-keyed comparison reports 102. Compare per KEY (parse both files into game_index -> units
maps and compare the intersection), never by line diff.

## What is NOT yet ruled out, in priority order

Serial determinism (above) narrows this list sharply: the cause must cross a game boundary or a
thread boundary.

1. **Scheduling-dependent cross-game state in a worker thread.** Still the leading candidate by
   shape: a batch worker owns its thread across many games, so any cache/memo that survives a game
   and is keyed loosely makes game N's work depend on which games ran before it on that thread —
   and the pool's interleave is not reproducible. Direct precedent: the m2 solve memo's dead
   cross-game entries once shifted cap-clear timing and broke work-unit thread-invariance.
   **Note the earlier elimination of this was UNDERPOWERED and should not be trusted.** It replayed
   two games (47, 67) alone versus after their predecessors and found them byte-identical — but at
   a 17% per-game rate, two games have only a 31% chance of showing the effect at all. Worse, it
   tested the *same* predecessor sequence, whereas the pool's defining property is that the
   predecessors DIFFER between runs. It is not evidence.
2. **Shared state that is thread-SAFE but not deterministic.** ThreadSanitizer being clean does not
   exonerate this class — a mutex-guarded or atomic cache has no data race and still returns
   different results depending on which thread arrived first. Everything eliminated earlier "by
   inspection" was eliminated for thread-safety, which is the wrong property. Re-examine
   `MulliganProfileIO`'s policy cache and sidecar-resolve map, `CardDatabase::LookupCached`'s
   `atomic_ref`, and any shared memo, asking only: *can a hit cost different work than a miss?*
   (`NameRegistry::Intern` is safe here on inspection: it hands back pointers into a node-based set
   whose element addresses are stable, and identity is preserved. `SubtypeRegistry` is safe by
   construction — ids are assigned during single-threaded load and the registry is frozen before
   the worker pool exists.)
3. **A wall-clock or timing input reaching a decision.** Budgets are deterministic work units
   (`NODES_PER_VIRTUAL_MS`), and every surviving `steady_clock` read is in `src/analyzer/`, which
   batch play does not use — so this is third, not first.
4. **Unordered-container iteration order** over pointer- or address-keyed maps. Note that heap
   addresses vary between runs even single-threaded (ASLR, and glibc's per-thread arenas), so if
   address ordering reached a decision the SERIAL runs should have diverged too. They did not,
   which argues against this — but only for the paths those 50 games exercised.

## Next step

**Running now: is it the per-decision MEMOS, as a class?** One 100-game job, twice per arm, 24
threads, `MTG_DUMP_UNITS=1`; arm ON is the shipped config, arm OFF sets
`MTG_SOLVE_MEMO=0 MTG_ENUM_MEMO=0 MTG_FS_NOWIN_CACHE=0 MTG_NO_BP_PREFIX_CACHE=1
MTG_NO_M2_SEARCH_MEMO=1 MTG_NO_BP_ENUM_CACHE=1`. Driver and results: `logs/edf_memo_off/`.
100 games gives P(false clean) ~ 1e-8 at the observed rate, so a clean OFF arm is conclusive.
This test is chosen because it settles hypothesis 1 for the whole memo class in one run instead of
bisecting caches one at a time — and because the earlier single-cache result
(`MTG_NO_BP_ENUM_CACHE=1`, 10/400 vs 8/400) only exonerated ONE of the six.

If OFF is also ~17%, the memo class is out and the next probe is a per-game unit-SITE partition
(the 13 `ConsumeAt(budget, unitsite::...)` calls; `MTG_ROLLOUT_STATS` prints that partition only as
a process aggregate today, so it needs a small per-game dump) on one fragile game such as 305.

The experiment binary is snapshotted at `logs/nondet_bin/mtg.nondet` (commit `56ba0979`) precisely
so that concurrent source edits by other work cannot silently change what is being measured
mid-investigation.

Note that a single-game repro needs `--game-index` — `--seed base+gi` alone plays a *different*
game, not game `gi` of that seed.
