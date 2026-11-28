# ds-memo: a verdict memo for the Dragonstorm round-4 payment levers (2026-11-28)

Branch `wip/ds-memo` (worktree `.claude/worktrees/ds-memo`, base origin/phase-1-2-deck-analyzer 524d7c303).
Scratch: `/workspaces/MagicDeckTester/build/scratch/ds-memo/` (binaries `bin_<tag>`, counts `ins_all.txt`).

## Verdict: NOT WORTH IT (below the 0.1% bar against the shipped binary)

The final design saves **0.059%** of Dragonstorm user instructions against origin's binary (0.15% against its
own memo-off arm), is play-identical on the 200 measured games, and is not provably exact. Recommendation: do not
land it. The branch holds the code (flag `MTG_LINE_REST_MEMO`, default ON in the code as written) only as a record.

## What was built (final shape, `src/ai/ManaPayment.cpp`, "MTG_LINE_REST_MEMO")
KeepOwedFloatPay's in-place route (plain payment trial, float check, summed judge, ordered walk, kept trial)
decides between the plain and the kept payment. The memo stores that verdict (1 plain / 2 kept) per game and per
thread, keyed on:
- `LrmPayDigest(board)`: every board field a payment / walk / judge reads that I could identify -- all three
  floating pools (every field), per permanent number / name hash / staged / tapped / summoning / control / animate /
  haste / pay marks (`mana_tap_mark`, `mana_flex`, `pay_sac_eaten`) / chosen colour, mask, subtype / every counter
  field and the counter list / damage / attachments; stack and exile by card; per player life, energy, poison,
  per-turn land / draw / lifegain counts, staged / suspended counts, hand (number, staged, name hash), graveyard by
  number, library by SIZE only;
- the bill, the rest's bill, the keep, `*available` (all compared exactly), the flags;
- `LrmCtx(at)`: PayContextDigest, CurrentPlanTraitsHash, the line-hold thread-locals the digest does not fold
  (`g_line_hold_has_acts`, reducer credit, noncreature mv / count-down, keep bound, unseen-carried, haste takers),
  PayingCreatureAbility, the paying card (name, number, staged), and the line's casts from `at` on (kind, name, def,
  cost, X, splice, float colour, alt/kicker/evoke/emerge/awaken/bestow/morph/soulfire).
A hit makes the stored payment directly; a stale KEPT verdict whose payment fails falls back to the route (so a
stale entry can never fail a payment the route would make). Stores refused under a muted recompute; table emptied
per game (TurnSolver::ClearPerGameCaches) and at 32k entries. `MTG_LINE_REST_MEMO_VERIFY=1` recomputes every hit
(route on copies vs stored payment on copies: result, board, pool); `MTG_LINE_REST_MEMO_STATS=1` prints counts.
Not exact because (a) the key is a digest (an unlisted field or a 64-bit collision would serve a wrong verdict) and
(b) the backtracker's mana cache is path dependent (McT2PayWord key holes), so skipping the route changes later
cache contents.

## Measurements (user instructions, `perf stat -e instructions:u`, Dragonstorm play, 200 games, levers ON,
`pg_on.json`, 2 threads; arm "off" = same binary with MTG_LINE_REST_MEMO=0)
```
variant                                                   memo on      memo off     on/off    hits
origin binary (ds-apply4 bin_c3, re-measured here)        181.298 G       -            -
m1  exact board (BoardsEqual + stored GameState),         183.629 G    181.488 G    +1.18%   rest 12%, KOF 36%
    both verdicts, 256-slot 2-way table
d3  exact board, keyed map (no set conflicts)             184.809 G    181.621 G    +1.75%   rest 55%, KOF 65%
d4  exact board, KOF verdict only                         183.087 G    181.623 G    +0.81%   KOF 65%
d4  PROBE: BoardHash key, no board (UNSOUND), both        181.193 G    181.623 G    -0.24%
d4  PROBE: BoardHash key, KOF only (UNSOUND)              181.205 G    181.623 G    -0.23%
d5  payment-board digest, KOF only                        181.305 G    181.580 G    -0.15%   KOF 69%
d6  d5 narrowed (rest memo deleted), route as a lambda    181.283 G    181.473 G    -0.10%
d7  route restored to its original shape                  181.199 G    181.470 G    -0.15%
d8  route + memo out of line (FINAL, committed)           181.192 G    181.465 G    -0.15%   KOF 47,270/67,834 = 69.7%
d8 memo on vs origin: 181.192 vs 181.298 G = -0.106 G = -0.059%
```
- Per piece: the KOF verdict memo is the whole effect (-0.15% same binary). The rest verdict memo (summed judge ||
  walk, at every site incl. the lump keep-owed rung) bought nothing even on the unsound hash key (-0.24% vs -0.23%),
  and cost +0.9% with exact boards; it was deleted.
- Exact-board verification cannot pay: copying a GameState per store and BoardsEqual per hit (Card ==, ManaCost
  ==, string ==) cost more than the route (profile of d3: copy ctor 2.8%, Card/ManaCost/string == ~1.4%).
- Why the probe's 73% / 91% repeat rates did not carry over: BoardHash omits fields that really differ between
  "repeats" (diagnostic on m1: permanents' `entered_this_turn`, `storage_counters` -- Mercadian Bazaar / Dwarven
  Hold are storage lands -- players' `lands_played_this_turn`, `suspended_cards`, and ~16k rest-verdict pairs
  differing in other members), and the rest verdict's context differs across the plans that reach a board.
- The memo-OFF arm of every memo binary sits ~+0.17 G (+0.09%) above origin although it runs the same route
  (checked: d8 route out of line, KOF's early exits unchanged in shape). Not attributed (most likely code generation
  of the TUs recompiled for the new header / slot); it is why the saving against origin is only 0.06%.
- Remaining memo cost (perf on d6): KeepOwedFloatPay self +~0.13% (digest, context, key), PayContextDigest 0.17%,
  plan-traits hash 0.07%. Even a free key would leave the saving near the unsound probe's 0.24%.

## Identity / correctness
- Every variant above: 200 / 200 Dragonstorm play jobs, digests AND units equal to origin (`ins_*.out` vs
  `ins_c3re.out`). So in practice play-identical on this sample.
- VERIFY run on d8 (200 games): 47,273 hits recomputed, **0 mismatches** (result, board, `available`). The exact-board
  m1 variant: 0 mismatches over 34k rest + 24k KOF hits.
- Not run, because the saving is under the bar (brief step 2): the 1,124-job valall identity sweep, the 45-deck
  counts, the oracle gate, the held-out A/B.
- Units: UN-RUN (the full mtg-test build at 2 lane slots under the gate's load was ~2.5 h; stopped when the
  verdict was reached). The new unit case (test/unit/test_dragonstorm_line_keep.cpp, "MTG_LINE_REST_MEMO -- a
  repeated owed-float entry pays exactly as the recompute") compiles (-fsyntax-only) but has not been executed.

## If someone wants to pursue it
The only shape that saves anything is a digest key (no board copies); the ceiling is ~0.24% of Dragonstorm (the
unsound BoardHash probe), and the context word (PayContextDigest over the ~590-slot arm) is a third of the memo's
own cost. A cheaper context (e.g. caching the arm's fold per job) and the unexplained +0.09% off-arm shift are the
two places left; neither looks able to clear 0.1% against origin by a margin that would justify a non-exact payer
memo.
