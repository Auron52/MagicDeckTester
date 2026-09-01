# The confounded bottoming gate now fails on some decks — Dragons and Mirrorwing

**Status: OPEN, two reproducible failures, first hypothesis REFUTED.** Filed 2026-09-01 because
`mulligan-profile.md` states the confounded bottoming A/B "has held on every profile so far", and
that is no longer true. Both failures are decisive rather than marginal, and both profiles are
correctly quarantined, so nothing bad has shipped — but the skill's claim needs correcting and the
mechanism is unknown.

## The measurements

Both generated with `mullgen.sh run <deck> complete` (R40, full bottoming), both gates run by the
driver as one operation. Negative = exhaustive wins; the bar is "at all worse on average".

| deck | keep (exh vs static) | bottoming (blind vs lookahead, CONFOUNDED) | seeds | mean/se | outcome |
|---|---|---|---|---|---|
| Minotaur (`5243288c`) | passes | **passes** | all agreeing | — | ADOPTED |
| Dragons (2026-08-30) | **−0.0472** ok | **+0.0641 WORSE** | **0/16** | +8.84 | quarantined |
| Mirrorwing v3 (2026-09-01) | **−0.0587** ok | **+0.1006 WORSE** | **0/16** | **+17.99** | quarantined |

Mirrorwing's spread: min +0.0500, median +0.1030, max +0.1410, sd 0.0224, se 0.0056. Every seed on
the same side, and the interval nowhere near zero.

**This is not label noise and more R will not fix it.** Noise scatters; 0/16 with mean/se of 8.8 and
18.0 is systematic bias. That matters because the skill's prescribed response to a bottoming failure
is "raise R or fix the heuristic" — the first half of that is not the answer here, and spending
another R40-scale generation to prove it would be expensive.

## Keep passes while bottoming fails, on both decks

That pattern is itself the strongest clue. Keep is the coarse decision (is this hand's mana/threat
balance playable at all); bottoming is an argmin over near-tie sub-compositions. Whatever is wrong
degrades fine-grained ranking while leaving coarse ranking intact — and on both decks keep gets
*better* by a comfortable margin at the same time.

## REFUTED: "the table is fit at a shallower depth than it plays"

The obvious hypothesis, and the one this document exists to kill so nobody re-derives it. Both
failing decks generate far below their play depth, so a table fit at d1–d2 mis-ranking sub-hands
that are then played at d5 looked like a complete explanation.

It is wrong. **Minotaur generates at `d1/b3` — shallower than Mirrorwing's `d2/b3` — and it
passes.**

| deck | `mull_gen_depth` | `mull_gen_budget_ms` | suite play | bottoming gate |
|---|---|---|---|---|
| Minotaur | 1 | 3 | d3 b10 / d5 b20 | **passes** |
| Dragons | 1 | 3 | d3 b10 / d5 b20 | fails |
| Mirrorwing | 2 | 3 | d3 b10 / d5 b20 | fails |

Depth-vs-play mismatch is therefore not sufficient, and probably not the mechanism at all.

## What has NOT been ruled out

* **A deck-shape effect.** Minotaur is linear aggro that wins ~T5 on curve; Dragons is ramp into a
  discontinuous payoff, and Mirrorwing is a combo/trick deck whose value concentrates in a few
  specific cards. Bottoming the wrong card plausibly costs far more on the latter two. This is a
  story, not a measurement — do not record it as a cause without evidence.
* **A value-leaf interaction.** All three decks ship value leaves, so it does not separate them on
  its own, but the leaf changes what a rollout scores and the table's argmin is computed through it.
* **A regression in the lookahead bottomer or in `MTG_CONFOUND_BOTTOM` itself** that happens to be
  visible only on some decks. Minotaur passing argues against a plain harness bug, but not against
  one whose effect is deck-dependent.

## How to attack it next

1. **Diagnose per game, the way the skill prescribes**: pull the losing `(seed, gi)` pairs from the
   two arms' win dumps (the game index is stable — same library both sides), log both with
   `--log-dir` to read the kept sub-hands, then re-score each kept sub-hand at high R with
   `MTG_SCORE_COMPS`. If the table's kept hand is blind-*worse* at high R, the table is mis-estimated;
   if it is blind-*better*, the loss is in how bottoming is applied at play time rather than in the
   table.
2. **Check the confound harness on a deck that passes.** Run `KM_MODE=bottom` with and without
   `MTG_CONFOUND_BOTTOM` on Minotaur and on Mirrorwing. If the confound flips Minotaur's sign but not
   Mirrorwing's, the correction is the thing behaving deck-dependently.
3. Only then consider R.

## What ships meanwhile

Both decks fall back to **static/default keep with lookahead bottoming**, which is the pre-existing,
validated behaviour — the quarantine renames the profile to `<stem>.keepmodel.exhaustive.profile.DISABLED.json`
and keep is presence-gated, so the fallback is automatic and complete. Both decks keep their adopted
value leaves, which are unaffected.

Note what is lost by the quarantine: Mirrorwing's keep half measured **−0.0587 t** and Dragons' **−0.0472 t**,
both real wins that the all-or-nothing gate discards along with the failing bottoming half. There is
deliberately no way to ship the table with bottoming off (`mulligan-profile.md`: it is not a knob),
so recovering that win depends on root-causing the bottoming failure — which is the strongest
argument for doing so.
