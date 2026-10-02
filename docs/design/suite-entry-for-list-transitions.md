# Suite entry during a list TRANSITION: carry both versions, retire the old one last

**Status: USER PROPOSAL 2026-10-01, measured and costed here, NOT yet written into CLAUDE.md.**
The user's words: *"It's a good point that maybe we should actually have new versions of lists in the
regression test. That might make sense to do and then we remove the old one once it has been fully
retired."* Phrased as a proposal, so CLAUDE.md's suite rule is left alone until it is confirmed. This
file is the standalone case for it, including the number that makes it practical.

## The problem it solves

CLAUDE.md requires **suite entry BEFORE the value leaf or mulligan profile**, because a value leaf and
a keep table are *fitted to a deck's play*, and with no suite case there is no ground truth, so no
engine change can ever be shown to have broken that play.

That rule and the way a list actually gets replaced are in direct tension:

* A new list cannot displace the incumbent until it is validated.
* Validating it needs a mulligan profile and (sometimes) a value leaf.
* Generating either needs it to be in the suite.
* But "one variant of a deck at a time" means it cannot enter while the incumbent is there.

So an adoption candidate is stuck: it is the thing that needs a digest tracking its play, and the
rule that demands one is also the rule that keeps it out. Fungus candidate-b sat in exactly this
state — a 350 MB banked generation journal on a list with no suite case.

## The proposal

Distinguish two populations that CLAUDE.md currently treats as one:

1. **Screening lists** (`<Deck>A`, `<Deck>B`, … — many, transient). Unchanged: *"NEVER COMMIT
   SCREENING / CANDIDATE LISTS — they are not final lists."* They live under `logs/` for the duration
   of the screen and never get a `decks/` folder or a suite case.
2. **An ADOPTION candidate** (one list, already screened, now being validated to replace the
   incumbent). This one DOES enter `test/regression_cases.sh`, **alongside** the incumbent. Both are
   tested for the whole transition. The incumbent's cases are removed only when the list is fully
   retired — i.e. when the new list is adopted into `decks/<Deck>/` and the old one is archived as
   `decks/<Deck>/v<N>-<slug>/`.

The archive convention already describes the end state; this only says what the suite looks like
*during* the overlap.

## Why it is affordable — MEASURED, and it is the load-bearing fact

The objection to carrying two variants of the heaviest deck in the suite is that it doubles that
deck's cost. It does not come close to mattering. At the suite's own `fungus` case shapes (d0 1000,
d3/b10 150, d5/b20 75) with the ms/game measured 2026-10-01:

| list | core-s added | core-min | % of the regression tier budget |
|---|---|---|---|
| shipped Fungus | 42.2 | 0.7 | 0.06% |
| **candidate-b** | **601.5** | **10.0** | **0.93%** |

The regression tier's budget is 45 min wall x 24 cores = 1080 core-min. So candidate-b is **14x the
incumbent and still under 1% of the tier**, and carrying both is under 1.1%.

## ADOPTED 2026-10-02, and the projection above was OPTIMISTIC — here are the real numbers

The cases are in (`[fungusb]`, half the incumbent's searched counts), GT accepted for all 8 keys.
Measured from the tier run that created them, not projected:

| tier | fungusb core-s | core-min | % of that tier's budget | makespan impact |
|---|---|---|---|---|
| smoke | 465.9 | 7.8 | 2.2% of 360 | 112s -> 141s |
| regression | 1564.7 | 26.1 | 2.4% of 1080 | 288s -> 325s |

So the honest figure is **~2.4%, not 0.93%** — the table above sized candidate-b off the *smoke*
case shapes and then compared against the *regression* budget. Still cheap, and the number that
actually matters for a shared box is the makespan: **+37s on the regression tier, +29s on smoke**,
because the pooled queue absorbs the extra games into the existing load-imbalance tail.

Two things worth recording from that run:

* **Cost is strongly seed-dependent.** s3003 is ~1.8x s2002 at BOTH searched depths (d5: 523.3s vs
  264.4s; d3: 482.5s vs 294.5s). Any future per-case sizing should expect that spread rather than
  treating one seed's cost as the cell's cost.
* **d5 buys nothing over d3 on this list, so far.** d3 5.4400/5.4600 vs d5 5.4800/5.4400 — flat, and
  one of the two seeds is nominally WORSE at the deeper search. At 50 games the quantum is 0.02/game
  so this is 1-2 games and settles nothing, but it is the first hint that candidate-b's depth
  economics deserve their own look, which per CLAUDE.md is a SETTINGS question (the depth matrix),
  not a search-hack one.

## The 3x rule is NOT what this is about, and the distinction matters

CLAUDE.md's 3x cost rule is **per-game at the deck's worst searched case**, and it is per-game
deliberately: *"total is confounded by the game counts we choose and so is gameable by sizing."* That
makes it a **tractability** judgement ("this deck is too slow to be worth searching at all"), not a
tier-budget constraint. The two come apart exactly here: candidate-b FAILS the per-game rule at
4032.5 vs a 3100.4 budget (1.30x) while costing under 1% of the tier.

So this proposal does not weaken the 3x rule and must not be read as a route around it. Two separate
decisions:

* **May an adoption candidate share the suite with its incumbent?** This proposal: yes.
* **Is a given deck tractable enough to test?** Still the 3x rule. The user has waived it *for
  candidate lists* (2026-10-01: *"I think its fair to have that rule be off for these candidates"*)
  while keeping the performance target: *"Ideally, we would get the performance in a position where it
  could be added to the regression test (even if we don't do so for now)."*

## A caveat on the gated metric, because it affects any verdict near the line

The 3x rule is denominated in **ms/game**, and on the current (shared) box wall is not reliable at
that resolution. The identical 30-game command measured **149.86 s and 80.91 s** of CPU on one binary
purely from the host's load, and the candidate-b/Fungus ratio moved **24x -> 38x** with it. A 1.30x
verdict sits inside that noise.

`units` is deterministic and contention-immune (budget converts via `NODES_PER_VIRTUAL_MS`, never read
off the clock). By units at matched seeds, candidate-b is **17.0x** shipped Fungus (232,242 vs 13,640
per game) where the budget is roughly 14-16x shipped — genuinely near the line rather than comfortably
over. If the 3x rule is ever re-litigated, the first change to consider is denominating it in units,
or at minimum recording the units alongside the ms in `test/suite_cost.json` so a borderline verdict
can be checked against a metric the box cannot move.

## What adopting this would change in practice

* `scripts/suite_gate.py --require` would pass for an adoption candidate once its cases exist, so
  `mullgen.sh run` / `valueleaf.sh run` no longer need `MTG_ALLOW_UNTESTED_DECK=1` — which is
  strictly better, because that override is the thing CLAUDE.md calls *"a USER decision, never an
  agent's"* and it suppresses the gate for every reason at once.
* Adding the cases means generating and accepting GT for them (an addition, not a rebaseline of
  existing keys).
* `tools/play/server.js` `listDecks()` enumerates every directory under `decks/`, so an adoption
  candidate is already visible in the viewer dropdown; that is correct for a list being validated by
  hand, and is the reason screening lists must stay out of `decks/`.

Related: `docs/design/fungus-candidate-b-generation-cost.md` (the 3x measurement and the lever gap),
`docs/design/per-deck-folder-layout.md` (the archive convention this transition ends in),
`docs/design/mullgen-cost-is-driven-by-bucket-count.md` (why candidate-b's generation is the
expensive one: K=22 from a five-type mana base).
