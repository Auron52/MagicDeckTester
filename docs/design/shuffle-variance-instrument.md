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

### CORRECTION — primary objective is avg win turn (losses = max_turns+1), NOT win%

The primary optimization objective is **average win turn with a loss counted as `max_turns+1`** (=9 at
the default horizon 8) — a single unified metric where fast wins are good and losses are penalised.
It is NOT "win% primary, avg-win-turn tiebreak" (an earlier mis-statement, incl. in the heuristic-
optimization skill, which should be corrected). Re-scoring the three arms on this metric (pooled over
seeds 2002/4004/5005, ensemble, losses=9):

| policy | avg turn (losses=9) | vs heuristic |
|---|---|---|
| original heuristic         | 5.9597 | —      |
| SANE (keep-Hinata-else-shuffle, COMMITTED) | 5.9247 | -0.035 |
| ALWAYS-SHUFFLE (shuffle every Ponder)      | 5.8692 | **-0.091** |

**Always-shuffle beats sane on every seed (-0.056 pooled).** So on the correct objective the ranking is
always-shuffle < sane < heuristic. The committed SANE change is still a real improvement over the
original (-0.035), but always-shuffle is the metric-optimal policy. Why always-shuffle wins even though
it shuffles Hinata away: Ponder keeps ALL 3 on top, so keeping a top containing Hinata also locks in the
2 junk cards beside her; the clairvoyant engine shuffles, dodges the junk, and re-finds Hinata later --
net faster, and the small win% cost of delaying her (captured by the loss penalty) is outweighed. The
"insane" intuition applies to a BLIND player; for the clairvoyant engine optimising avg-turn it is
coherent. ADOPTION OF ALWAYS-SHUFFLE pending user decision (bigger behaviour change; user had flagged
it as counterintuitive). If adopted: missing-Hinata AND Hinata-online branches -> shuffle; re-validate; rebaseline.

---

## Clairvoyance-DECOUPLING instrument (`MTG_SHUFFLE_SALT_SEARCH`) — the always-shuffle "edge" is an artifact

**Resolution of the always-shuffle question (2026-07-05).** The shuffle-variance ensemble above
averages over reshuffle *realisations*, but within each salt the clairvoyant search still sees that
salt's future and can time Ponder/Preordain around the reshuffle it is about to get. So the ensemble
strips reshuffle *luck* but NOT the search's ability to *pre-see which reshuffle it gets*. That residual
is exactly where "always-shuffle looks better" could be a clairvoyance artifact rather than a real edge.

**The instrument.** A second salt, `GameState::shuffle_salt_search`, used by mid-game shuffles ONLY
while the engine is *evaluating* a line (thread-local `g_shuffle_eval`, set inside `SimulateToEnd`,
`EnumerateEarliestWins`, and `RolloutWinTurn`). The real committed application uses `shuffle_salt`.
When the two differ (`MTG_SHUFFLE_SALT_SEARCH=k`), the search plans against a reshuffle the executor
will NOT deal — so a decision that only wins because the search foresaw a specific reshuffle collapses,
while a decision that is good on its *features* survives. Everything else stays clairvoyant (this is not
Monte-Carlo play — it is an analysis-only strip). Defaults equal to `shuffle_salt` → byte-identical,
lockstep intact (verified: seed 2002 no-env == `MTG_SHUFFLE_SALT_SEARCH=0`, 5.947). Decoupling is
demonstrably active: play degrades (5.947 → 6.1) because the search's foresight is now wrong.

**Result (Hinata, 150g seed 2002, d5 b20; decoupled averaged over salts 1–4):**

| regime | heuristic metric | always-shuffle metric | shuffle − heuristic |
|---|---|---|---|
| COUPLED (normal clairvoyant play) | 5.900 | 5.807 | **−0.093 → shuffle better** |
| DECOUPLED (search blind to true reshuffle) | 6.015 | 6.077 | **+0.062 → shuffle WORSE** (all salts agree: +.08/+.05/+.08/+.03) |

**The always-shuffle advantage is 100% clairvoyance.** Strip the search's ability to pre-see which
reshuffle it gets and always-shuffle flips from better to *worse* than the feature-based keep. It is
NOT a benchmark/ceiling to chase — it is the same class of artifact the granular gate sweep finds, just
via a cleaner tool. Corroborated by a depth sweep (edge grows d2→d5: −0.075 → −0.093, and REVERSES at
d0 where the heuristic wins outright) — though d0 is a degenerate greedy player, so the decoupling
instrument is the load-bearing proof. This retires the "clean the heuristic until it beats always-shuffle"
goal: the goal is instead to pick the keep-rule that is best under the clairvoyance-STRIPPED (decoupled)
metric, since coupled play lets the search mask keep-rule quality (all rules within ~0.02t coupled).
