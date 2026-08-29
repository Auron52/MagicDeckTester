# The 168 UNPRICED prepay rows (mostly Hinata) — DEFERRED, and what would unblock them

**Status:** deferred, not started. Handed to the mana-focused workstream (user direction,
2026-08-26). This note exists so the item lives in git rather than in one agent's head.

**Parent document:** [`prepay-payment-path-recheck.md`](prepay-payment-path-recheck.md) — §2d is the
ledger this note is about, §7 records it as "the least settled part of that document". Read §2d
before doing anything here; do not re-derive its classification.

## What the rows are

When the prepay true-colours fix landed, 418 games changed result. §2d prices the **pre-fix** payment
of every one of them to answer a single question: *was the old line paying with mana the rules
actually allowed?* Each game lands in one of three buckets:

| verdict | n | meaning |
|---|---|---|
| LAUNDERED | 118 | the old payment was rules-illegal. The game getting worse is the fix **working** |
| LEGAL | 132 | every priceable turn balances under true colours; something else moved this game |
| **UNPRICED** | **168** | the model refuses to price at least one turn — it can say nothing either way |

The UNPRICED rows are not a backlog of *bugs*. They are the rows where the **measuring instrument**
abstains, so their 168 games are currently unattributable in both directions: we cannot claim them as
laundering the fix correctly killed, and we cannot claim them as regressions worth recovering.

By deck the concentration is extreme:

| deck | n | LAUNDERED | LEGAL | UNPRICED |
|---|---|---|---|---|
| **hinata** | 208 | 56 | 14 | **138** |
| mirrorwing | 84 | 36 | 30 | 18 |
| dragonstorm | 37 | 24 | 5 | 8 |
| everything else | 89 | 2 | 83 | 4 |

Hinata alone is 138 of the 168, and Hinata's own split (56 / 14 / 138) means **two thirds of that
deck's changed games are unadjudicated**. Any statement about what the prepay fix cost Hinata is
therefore resting on 70 priced games out of 208.

## Why the model abstains

Three distinct causes, and they need separating before any of them is worth work:

1. **An unresolved `{X}` in `manaPaid`.** The ledger balances a turn by summing what the sources can
   produce against what the costs demand. An `{X}` cost's demand depends on a value the log does not
   carry at that point, so the turn cannot be balanced either way.
2. **A Reality Spasm untap-refloat.** "Untap X target permanents" is modelled as *floating X mana*
   rather than as an untap (see [`reality-spasm-phase2.md`](reality-spasm-phase2.md)), so the mana
   appears from nowhere as far as a production ledger is concerned. This is a **modelling** gap, not
   a pricing gap — fixing the model is what retires this cause, and that is phase-2 work.
3. **Sources short on raw count.** The turn demands more total mana than the model believes the board
   produces. §2d is one-sided by construction: it only ever *accuses* (LAUNDERED requires enough
   TOTAL mana in the wrong COLOURS), so where the count itself falls short it abstains rather than
   guess. That usually means the model is missing a production path, not that the engine cheated.

Cause 3 is the one most likely to be cheap: a missing production path is a bug in the pricing model
that can be found and closed, and every row it retires moves to a real verdict. Cause 1 needs the
`{X}` value threaded into the log. Cause 2 is blocked on the Reality Spasm model and should not be
attacked from this side.

## What would unblock them

In rough order of value per unit of work:

1. **Split the 168 by cause.** Nobody has done this. The three causes above are the *known* reasons
   the model abstains, but their proportions are unmeasured — and the right next action differs
   completely depending on whether Hinata's 138 are mostly `{X}` or mostly short-count. This is a
   report over existing data, not a new measurement, and it should come first.
2. **Close cause 3 if it dominates.** Audit the pricing model's production table against the decks
   involved; each missing path retires a batch of rows at once.
3. **Thread the resolved `{X}` into `manaPaid`** if cause 1 dominates. This is an instrumentation
   change to what the log records, not an engine behaviour change.
4. **Leave cause 2 alone** until the Reality Spasm model is fixed. Pricing a refloat that the engine
   itself models wrongly would only encode the wrong model into the ledger.

## Why this is deferred rather than dropped

The prepay fix is already adopted and its ground truth rebaselined; nothing here blocks anything
shipped. What the unpriced rows block is a *claim* — any confident statement about what the prepay
true-colours fix cost or saved on Hinata. Until they are split by cause, the honest phrasing stays
the one §7 uses: 118 LAUNDERED is a **lower** bound, 132 LEGAL means "no laundering this model can
see" rather than "audited clean", and Hinata is the least settled deck in the set.

Related: [`prepay-payment-path-recheck.md`](prepay-payment-path-recheck.md) (the ledger and its
honest limits), [`reality-spasm-phase2.md`](reality-spasm-phase2.md) (cause 2's blocker),
[`mana-source-reservation.md`](mana-source-reservation.md) (the neighbouring deferred mana item).

## 2026-08-29: the split, measured (step 1 done) — with a stability caveat

`legality_one` re-run over all 418 rows (engine at the M2_RECONSIDER adoption tree, legacy
levers pinned + `MTG_M2_RECONSIDER=0`; driver bypassed the tsv write-back deliberately —
the committed `spend` column remains the 2026-08-26 truth). Detail:
`logs/mana_robust/prepay_legality_rerun.json` (gitignored; regenerate with the same driver).

**The split of today's 183 UNPRICED rows by abstain cause:**

| cause | rows | share |
|---|---|---|
| sources short on raw count (cause 3) | 111 | 61% (106 pure + 5 mixed) |
| unresolved `{X}` in a cost (cause 1) | 77 | 42% (72 pure + 5 mixed) |
| Reality Spasm refloat (cause 2) | — | not separately visible: Spasm is itself an `{X}` spell, so its rows land in the `{X}` bucket |

By deck: hinata 149, mirrorwing 20, dragonstorm 9, others 5. **Short-count dominates**, so per
this doc's own ordering the next action is №2: audit the pricing model's production table
against hinata's board states — each missing production path retires a batch of rows at once.
Threading the resolved `{X}` (№3) is second.

**Caveat that must travel with these numbers:** the re-run drifted from the committed 2026-08-26
classification on 64 of 418 rows (LAUNDERED 118 -> 68, LEGAL 132 -> 167, UNPRICED 168 -> 183).
The legacy-lever env restores only the six pinned behaviours; the rest of the engine has moved
(minotaur exact-discount, auras searched dig, M2R-era plumbing), so the control replays are not
the 2026-08-26 games. This confirms the parent doc's commit-bound warning empirically: the
ledger is a measurement of (engine, env), not a stable property of the seed list. Any future
pricing work should regenerate its own snapshot first and diff against the tsv before trusting
either.
