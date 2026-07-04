# Shuffle-variance instrument (independent shuffle salt)

A general primitive for evaluating STOCHASTIC (shuffle-triggering) decisions in the clairvoyant
engine — and de-confounding any heuristic A/B from shuffle luck — WITHOUT breaking the clairvoyance
the search depends on (commit-the-line, lookahead-bottoming, lockstep rollouts).

## Why this exists

The granular gate sweep (`heuristic-optimization.md`) can only rule out CLAIRVOYANT gaps: it compares
"heuristic" vs "clairvoyant search," never "heuristic" vs "a better blind rule / the E-optimal
stochastic choice." For a decision whose value depends on a downstream SHUFFLE (Ponder keep-vs-shuffle,
fetch, tutor, mulligan, Gamble), a single seeded run gives ONE reshuffle — so "shuffle won on seed S"
can be shuffle luck the clairvoyant search exploited, not evidence the decision was right in
expectation. You need to average over many reshuffles.

Full Monte-Carlo (randomising the decision-maker) is a non-starter: the engine DEPENDS on a single
deterministic seeded future (commit-the-line predicts a specific line; bottoming rolls out clairvoyantly).

## The mechanism

Every shuffle in the engine funnels through a deterministic seed keyed on `game_seed`
(`SearchShuffleSeed` for mid-game search/Ponder/fetch/tutor; `game_seed + mulligan_count` for
mulligans; a mix for Gamble). We fold an INDEPENDENT salt into each via `SaltSeed(base, salt)`
(`salt==0` = identity => byte-identical default). Two `GameState` fields, copied through search
deep-copies so rollout+executor share the salt (lockstep preserved):
- `shuffle_salt` (`MTG_SHUFFLE_SALT`): salts MID-GAME shuffles only -> **same opening, different
  in-game shuffle realisations**.
- `shuffle_salt_opening` (`MTG_SHUFFLE_SALT_OPENING`): also salts the initial deck shuffle + mulligan
  reshuffles -> vary the opening too.

The trick that preserves clairvoyance: we do NOT make one game stochastic. We hold `game_seed` and all
decisions fixed and sweep the salt, so **each sample is an ordinary deterministic-seeded game** the
clairvoyant search plays out unchanged. The ensemble mean over salts is the shuffle-luck-averaged
expectation. Per-decision isolation (vary only shuffles at/after index k) falls out of the
`search_count` keying for free — no state-fork machinery.

## Harness

- `scripts/shuffle_ensemble.sh <manifest> <K> <outdir> [OPENING]` — runs a manifest across K salts.
- `scripts/aggregate_ensemble.py <outdir>` — per-game win-prob + mean win-turn + count of
  SHUFFLE-SWINGY games (won in some salts, lost in others = where the shuffle realisation flips the game).

## First application — Hinata Ponder keep-vs-shuffle (2026-07-04)

200 games, seed 2002, d5 budget 20, 6 salts. A temp `MTG_PONDER_FORCE` (1=keep, 0=shuffle) forced the
policy; each arm ensemble-averaged over the salts:

| Ponder policy | ensemble win% | mean win turn | swingy |
|---|---|---|---|
| HEURISTIC (ships) | 96.33% | 5.819 | 1.5% |
| SHUFFLE always | 96.50% | 5.725 | 3.5% |
| KEEP never | 88.33% | 5.904 | 1.5% |

**Findings:**
1. The keep-vs-shuffle decision is the **highest-stakes lever found in Hinata: ~8.2pp** (96.3 vs 88.3) —
   dwarfing the deterministic ponder levers (reorder order <0.7pp, keep-set selection <1.7pp). Shuffling
   away a dead top to dig fresh toward the combo is worth a lot.
2. **The heuristic captures ~all of it**: 96.33% vs always-shuffle 96.50% — within noise (~0.3 games).
   For this combo deck, shuffling when missing Hinata is almost always correct, and the heuristic
   effectively is "always shuffle when she's missing." NOT losing meaningful games on this decision.
3. A **tiny residual**: always-shuffle is marginally faster (5.725 vs 5.819 mean win turn, a more
   consistent signal than the flat win%). Hints the heuristic's "keep if the top advances toward Hinata"
   exception occasionally keeps a top slightly worse than a fresh dig. Sub-noise on win%, so NOT adopted
   on one seed -- a testable hypothesis for a proper multi-seed sweep if ever worth chasing.
4. **Shuffle LUCK barely matters**: only 1.5-3.5% of games are swingy (flip on which reshuffle you get).
   The combo deck is robust to WHICH reshuffle, sensitive only to WHETHER you shuffle. So the heuristic
   should (and does) spend its attention on the keep-vs-shuffle CHOICE, not the reshuffle outcome.

**Net:** the Ponder heuristic is validated near-optimal on its highest-stakes (stochastic) decision.
Combined with the deterministic-lever bounds, the whole ponder/dig heuristic is measurably sound.

### Follow-up: the always-shuffle edge REPLICATES (candidate improvement)

The seed-2002 hint (item 3) was validated on two HELD-OUT seeds (4004, 5005), 200g/6-salt ensembles
each (heuristic vs forced always-shuffle):

| seed | HEUR win% | SHUF win% | Δwin% | HEUR wt | SHUF wt | Δwt |
|---|---|---|---|---|---|---|
| 2002 (tune)     | 96.33 | 96.50 | +0.17 | 5.819 | 5.725 | -0.094 |
| 4004 (held-out) | 95.58 | 95.75 | +0.17 | 5.888 | 5.821 | -0.066 |
| 5005 (held-out) | 93.75 | 94.33 | +0.58 | 5.779 | 5.710 | -0.069 |
| **pooled**      | 95.22 | 95.53 | +0.31 | 5.829 | 5.752 | **-0.077** |

Same direction on ALL three seeds (higher win% AND faster) -> a real, small, validated edge, not noise.
Also a SIMPLIFICATION: drop `KeepReorderTop`'s missing-Hinata "keep if dig>=1 && (dig+useful)>=2"
exception and always shuffle when Hinata is missing. Mechanism: Ponder keeps ALL 3 on top (can't bottom
the junk 3rd card), so re-digging past a mediocre top finds combo pieces a hair faster than being locked
into it. (The force-arm shuffled on EVERY Ponder incl. the rare Hinata-online case; the gain is almost
certainly the common missing-Hinata regime, so the clean adoption is the missing-Hinata branch only.)

Magnitude is small (~0.077 turns / +0.31pp) but consistent and never-regressing. ADOPTION PENDING user
approval (it shifts Hinata play -> GT rebaseline). Clean adoption path: change the missing-Hinata branch
to `return false`, re-confirm the SHIPPED change (heuristic vs new, no force flag) reproduces the edge,
then --accept the Hinata GT across modes. Scaffolding (`MTG_PONDER_FORCE`) reverted.
