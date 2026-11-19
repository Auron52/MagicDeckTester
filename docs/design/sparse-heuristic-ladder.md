# The sparse heuristic ladder (2026-11-19)

The **sparse heuristic ladder** is the rollout ladder with the rollouts it does not need skipped. It is selected per
deck by `value_play.ladder = "sparse"` in `<deck>.value.json` and is, in code, sampled-gate mode 4
(`valuearm::t_deck_sampled_gate == 4`, `p18::Knobs()` in `src/ai/TurnSolver.cpp`). Its research history is P18
(`docs/design/sampled-gate-ladder.md`, iterations 1-11 and P18i) and P18j (`docs/design/p18-claude-handoff.md`).
This document is the canonical description; those two are the measurement logs.

## USER rulings (verbatim)

- 2026-10-24: *"It should be possible to replace an expensive rollout ladder with a leafless shape or equivalent up
  to the final rollout. Doing early rollouts for little to no benefit makes no sense."*
- 2026-11-13: *"My preferred vision would be that it uses less rollouts when more certain and more when less
  certain. So, it would be closer to the heuristic rollout, but would still bank savings."* *"There shouldn't be
  cases where it is so uncertain it just runs the ladder."* *"It should at least cut out a significant chunk of it."*
  Uncertainty drives more or fewer SAMPLED rollouts under a hard fractional ceiling; it never falls back to the
  whole heuristic ladder.
- 2026-11-17: *"We shouldn't leave any of these open. If there are cases where the mode loses to the heuristic
  ladder we should keep investigating."* Ruling 3: valueleaf.sh gains a stage that measures the mode against the
  heur ladder when the ladder is the shape that would ship, and turns it on when it wins.
- 2026-11-19: *"I don't want the heuristic ladder for any deck."* *"Yes, I think we should finish P18 (or whatever
  it is called) on the branch and probably use a better name like sparse heuristic ladder or something like
  that."* *"The idea is that I want to actually skip some of the rollouts we don't need. I don't see an advantage
  to doing all of them."* *"Delaying the rollouts to the end is also beneficial, but does not encompass my full
  design."*
- 2026-11-19, scope: *"I was primarily just thinking about 1 here. 2 is possible if you can come up with lossless
  ideas for it, though."* -- (1) = the rollouts of SUPERSEDED passes (a deeper pass replaces them), (2) = leaves
  inside the committing pass that cannot change the decision. Bar for (2): *"I guess being no worse on average is
  okay for the bar here, since we aren't impacting the search. This is just the rollouts which already make
  mistakes. However, this rule should apply to just your prunes in 2 in isolation."* *"And, to be clear, it does
  need to be better on some axis. In this case we expect to improve performance. If the quality is even and
  performance better, that is sufficient."* So each (2) prune is A/B'd alone ((1) vs (1) + prune, held out):
  net <= 0 turns AND units/CPU measurably lower.

## The keys

| sidecar `value_play` | engine | status |
|---|---|---|
| `"ladder": "sparse"` | sampled gate 4; any learned model in the file is set aside (`AttachValueSidecar`); `leaf: "none"` overrides it | the shape |
| `"sampled_gate": 4` | the same mode, the research spelling | kept |
| `"sampled_gate": 3` | P18i leafless lookahead, ladder fallback (Boars, Morph, Spirits) | shipped; explicit key wins over `ladder` |
| `"sampled_gate": 2` / `1` | P18 priced sampled gate / raw sampler | research |
| `"sampled_gate": 5` | RESEARCH (2026-11-19): mode 4, then mode 2's priced sampled gate on a miss instead of the ladder | screen arm |

The gate acts only when no value model is attached (`p18_on` requires `!vl_active`), at the top-level decision
(`p16::t_hyb_nest == 1`, not inside rollouts), at every budget.

## What mode 4 does

One rollout of the root (`FSLineWin` depth 0, cut at the window edge, capped at 1% of the decision budget in units
and 10% in priced work, skipped for the rest of a game after two consecutive cuts). If it wins inside the window at
turn W, leafless passes 1..W-T+1 are walked, each admitted only where the ladder's own start gate would admit the
rollout pass it stands in for (priced `l_k + n_k * q * max(0, W-T-k)`); the target pass and cheap leafless passes may
run past that reach under an allowance. The first proven in-window win commits, for the current turn only (no
cross-turn replay). Without a commit, every unit and every priced nanosecond the lookahead spent is refunded and the
ordinary ladder runs on exactly its own budget. The whole lookahead is isolated: its own memo epoch, a scratch table
for the root rollout, no stores into the game-level caches.

At budget 0 nothing becomes unfindable: a lookahead commit is a proven in-window win (the earliest, since every pass
below it was refuted), and a miss leaves the ladder untouched.

## Which shapes run the heuristic ladder

(Filled from the 2026-11-19 census; see the next section.)

## Measurements

(2026-11-19 pooled held-out A/B; see below.)
