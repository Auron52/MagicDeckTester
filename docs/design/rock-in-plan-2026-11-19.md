# A mana rock cast in the line: the line-keep rung with the rock on the board (`MTG_LINE_KEEP_ROCK`), 2026-11-19

Follow-up to `line-keep-all-2026-11-18.md` (`MTG_LINE_KEEP_ALL`, default OFF by the USER's call) and
`dragons-incubator-drop-2026-11-18.md` (`MTG_LINE_REDUCER_KEEP`, default ON). The residual both docs named --
"the rest is paid partly by the mana rock the line casts" -- and the Stuffy Doll family of the mana-clean gate
(`logs/mana_clean/claude-queue-20261119g2`: 45 APPLY-DROP, 14 APPLY-DROP-ORDER, 3 ORACLE-GAP over 18 games).

## The mechanism (confirmed by trace)

1. **The joint prepay declines every line that casts a producer.** `TurnSolver::BatchPrepayMainCasts` returns
   `PP_PRODUCER` for a line with `a.rock_mana.Total() > 0` (or a ritual float) unless `MTG_PREPAY_PRODUCER=1`
   (closed OFF, `search-recoverability-audit.md` 4b). Stuffy s30003847 under `MTG_PREPAY_PROBE=1`: 26.8% of all
   batch-prepay calls are `declined: producer (ritual/rock)`.
2. **So those lines are paid cast by cast by the greedy, with no joint allocation**, and the only joint-keeping net is
   the line-keep rung (`LineReducerKeepHold`, ManaPayment.cpp). It is gated on
   `g_line_reducer_credit > 0 ? LineReducerKeepOn() : g_line_keep_bound`; a rock line has no reducer credit and
   `MTG_LINE_KEEP_ALL` is OFF, so the rung never ran (s30003847 off arm: `[line-reducer-keep] trials=0`). Stuffy
   s30003847 T4 `{Guilty Conscience W, Mind Stone 2, Loran's Escape W}` on Plains + Boros Garrison + Mind Stone: the
   greedy pays Mind Stone with Garrison's {R}{W}, and Loran's Escape's {W} then meets two Mind Stones -> APPLY-DROP.
3. **Even where the rung ran, it judged the rest on the board BEFORE the rock resolves**, so a rest that needs the new
   rock's mana read "joint unpayable" and never fired (Dragons s30094937 T4 `{Dragonspeaker Shaman 1RR, Mind Stone}`).
   `RampLaterPayable` already had the right rule (the paid-for rock on the rest board); the rung lacked it.

## The fix (`MTG_LINE_KEEP_ROCK`)

* `LineKeepBound` (TurnSolver.cpp) binds a line of >= 2 held casts one of which is a rock that enters untapped
  (`LineCastIsRock`: `mana_rock`, non-creature, not `enters_tapped`, not a pure converter such as Arcum's Astrolabe).
  These are exactly the lines `PP_PRODUCER` hands to the greedy. Fire Diamond (enters tapped) is excluded by design.
* In the rung, every rest judgement (witness, complete judge, hold-out and table boards) puts the rock being paid for on
  the board (`LineKeepRockFor` / `PutLineKeepRock` / `LineBillPayable(..., rock)`).
* Searched, not greedy: the rung only re-makes a payment the greedy would strand, choosing among payments that keep the
  rest payable; it restricts the payer's options, never picks among search branches.

### CPU: an exact tightening of the rung's joint test (commit dfc8a326e + the ramp-filter extension)

The first build cost the on arm +3.2% CPU over the whole A/B (Niv +8.8%, Stuffy +5.3%, White Knights +5.2% with zero
play change). Phase timing (`MTG_LINE_REDUCER_KEEP_STATS`, print-only) on a 400-game probe put 57% of the rung's time in
**strands no repair can keep**, all on the rock's own payment: the joint test on board + rock lets the rock pay its own
cost, so a hopeless strand passed and ran the whole hold-out subset search and the oracle option table.

The tight test: the rock cannot pay itself, so the board alone must pay this bill plus the rest net of what the rock can
meet --
* a colourless rock (Mind Stone, Sol Ring): `produces_amount` of the rest's {C} pips first, then generic;
* a ramp filter (Izzet Signet): one pip of each colour it makes (or a generic), plus the {1} that feeds it.

That is a NECESSARY condition of every repair, so the set of repairs, and play, is unchanged. It is NOT valid, and is
skipped, on a board with an untapped generic-fed converter (ramp/any-colour filter: the rock's {C} comes out coloured),
any sacrifice-for-mana source (Generator Servant, a sac outlet, an on-demand crack: the option table taps that mana and
the joint backtracker does not), or a deck with a mana untapper. Both guards were found by `MTG_LKR_TIGHT_CHECK=1`
(diagnostic: runs the loose path after a tight refusal and prints any repair the tight test would have refused): the
first cut lost 261 of 68,607 repairs, the converter guard left 239 (40 of 40 dumps were Generator Servant cracks),
and with both guards it prints none and every repair count and digest matches.

Probe (Niv / White Knights / Stuffy / Angels x 100, lever on): rung time 21.7 s -> 13.2 s with the colourless cut
(RESULTS FOR THE RAMP-FILTER EXTENSION AND THE FINAL A/B CPU: see below).

## Repro flags (MTG_MANA_ORACLE=2 over every flagged game; arms off / MTG_LINE_KEEP_ALL / MTG_LINE_KEEP_ROCK)

| deck (games) | class | off | KEEP_ALL | KEEP_ROCK |
|---|---|---|---|---|
| Stuffy Doll (18) | APPLY-DROP | 45 | 10 | **0** |
| | APPLY-DROP-ORDER | 14 | 14 | 14 |
| | ORACLE-GAP | 3 | 3 | 3 |
| Dragons (109) | APPLY-DROP | 218 | 117 | **75** |
| | ORACLE-GAP | 152 | 152 | 152 |

Dragons APPLY-DROPs on plans casting Mind Stone 98 -> 17, Sol Ring 59 -> 4, Fire Diamond 18 -> 18 (enters tapped, out
of scope). 57 of the 75 left name Haven of the Spirit Dragon's creature-only {R}: a separate restricted-mana family.

Residuals, NOT this family:
* **Stuffy APPLY-DROP-ORDER x14**: `{Chain Reaction + Blasphemous Act}` (12) -- Blasphemous Act costs {1} less per
  creature, so casting Chain Reaction first raises its cost; the oracle's other order pays. A cost-reduction ORDER
  question, not a payment.
* **Stuffy ORACLE-GAP s30000027 T4** `{Inspiring Vantage + Guilty Conscience@35 + Mind Stone + Mogg Maniac}`
  (unchanged by the lever): the oracle reaches the engine's board at its pay3 frontier (`pay1[Boros Garrison]`,
  `pay2[Mountain, Mind Stone#32]`, `pay3[Inspiring Vantage:W]`, Mind Stone#33 untapped), but its enumeration carries
  a fourth payment `pay4 cost={1}` that the engine's realisation does not, so every one of its 26 outcomes taps the new
  Mind Stone#33 ("tapped by oracle only; float engine - oracle {C}"). Oracle-side; the source of the extra `{1}`
  frontier was not pinned (open item for the mana-oracle owner).

## Held-out A/B (seed base 660000, paired seed = BASE + gi, play settings; 24,600 pairs, one pooled batch)

| deck | pairs | faster | slower | net turns (on-off) | digest-only | CPU on/off (first build) |
|---|---|---|---|---|---|---|
| alchemist (control, no rock) | 100 | 0 | 0 | 0 | 0 | 1.0035 |
| angels | 1000 | 0 | 0 | 0 | 0 | 1.0234 |
| bruna | 1000 | 2 | 0 | -2 | 7 | 1.0163 |
| burn (control) | 500 | 0 | 0 | 0 | 0 | 1.0011 |
| dragons | 5000 | 1 | 0 | -1 | 14 | 1.0329 |
| extraturns | 1000 | 0 | 0 | 0 | 0 | 1.0300 |
| fivecolour | 1000 | 0 | 0 | 0 | 1 | 1.0133 |
| fungusv3 | 1000 | 0 | 0 | 0 | 0 | 1.0071 |
| galadriel | 1000 | 2 | 0 | -2 | 1 | 1.0132 |
| giants | 1000 | 0 | 0 | 0 | 0 | 1.0130 |
| goblins (control) | 500 | 0 | 0 | 0 | 0 | 1.0015 |
| hinata | 1000 | 1 | 0 | -1 | 4 | 1.0192 |
| kittyv3 | 1000 | 0 | 0 | 0 | 0 | 0.9992 |
| merfolkturns | 1000 | 0 | 0 | 0 | 2 | 1.0031 |
| niv | 1000 | 3 | 4 | **+1** | 71 | 1.0883 |
| sea | 500 | 0 | 0 | 0 | 0 | 1.0074 |
| stompy | 1000 | 0 | 0 | 0 | 0 | 1.0188 |
| stuffy | 5000 | 1 | 0 | -1 | 4 | 1.0530 |
| whiteknights | 1000 | 0 | 0 | 0 | 0 | 1.0521 |
| ALL | 24600 | 10 | 4 | -6 | | 1.0315 |

**Niv-Mizzet Combo re-measured** on 2,000 fresh pairs (seed base 670000): 17 faster, 3 slower, **net -14** (CPU 1.0779,
first build). Pooled with the first 1,000: 20 faster, 7 slower, net -13 over 3,000 pairs.

**Every slower game recovers at `--depth 8 --budget-ms 0`** (both arms identical turn AND digest):

| deck | gi | off | on | d8 b0 off | d8 b0 on |
|---|---|---|---|---|---|
| niv | 65 | 6 | 7 | 5 | 5 |
| niv | 322 | 5 | 6 | 5 | 5 |
| niv | 373 | 4 | 5 | 4 | 4 |
| niv | 541 | 4 | 5 | 4 | 4 |

Root cause of the Niv slower games: they diverge at T1 (Ponder vs Preordain) or T3 (dig order), never on a payment of
the turn played -- the rung changes payments inside lookahead lines that cast Izzet Signet / Sol Ring, which re-ranks a
budgeted root. Budgeted-search ranking noise from a more faithful payment; it recovers unbudgeted.

## Niv-Mizzet Combo CPU (the one deck whose rung cost shows; agent 3 + agent 4, 2026-11-20/21)

Where it goes (200-game probe, seeds 680000, lever on, `MTG_LINE_REDUCER_KEEP_STATS=1`): the rung is ~8% of the on
arm's CPU, and that is the whole on/off gap (ab3: units 0.9942, CPU 1.0650 -- the extra CPU is per-unit cost). Inside it,
the oracle table (`manaoracle::MinimalPayments`, mostly `PayEnum::Verify`) is ~41-55%, the greedy trial / hold-out
payments ~32%, GameState copies ~8%. About 1/3 of the tables fire; the rest are no-fire strands, which need the whole
table to prove that no option works.

Exact cuts taken (play byte-identical; every one checked by `.wins` identity):

1. **In-place rest judge** (`LineBillPayableOn`): the rest is judged ON the scratch board the caller discards, which
   saves one GameState copy per held-out set paid and per table option made. Rung 16.6 -> 15.8 s / 200 games.
2. **Two-step (prefix) table ask** (`MTG_LKR_TABLE_PREFIX`): MinimalPayments' lazy stop returns a PREFIX of the same
   table, so a small ask is made first and the full one only when none of its options keeps the rest. Paired 1-thread
   probes (same 200 games, run side by side so contention is shared; rung ms): prefix 0 (one full ask) 18.4 s,
   1 -> 19.4 s, **2 -> 14.7 s**, 4 -> 15.0 s. Most repairs fire at the table's SECOND option (fire index 0/1/2/3+ =
   6157 / 30821 / 1280 / 11586), so a prefix of 1 pays two asks for them; the default is 2 (-20% of the rung, ~-1.3% of
   the on arm's CPU). Every prefix value gives byte-identical games (0/1/2/4 on the 200 games; prefix 1 on the 4,800-game
   exactness slice below).
3. **Signet-pair tight test** (`MTG_LKR_SIGNET_PAIR`, default ON, commit da3d3cb61): an untapped board converter of the
   ramp-filter rock's own shape ("{1},{T}: Add {a}{b}", the same two colours -- a second Izzet Signet, Ferrous Lake) no
   longer turns the rock's tight joint test off. The proof is in the code comment (drop the rock from a repair: f, its
   feed, re-feeds the first converter a rock output fed, an {a}/{b} unit from that converter's outputs re-feeds the
   second, so the board alone pays all but one unit, whose colour is chosen to fit rest - m_a - m_b + {1}). Verified by
   chain7: units, `MTG_LKR_TIGHT_CHECK=1` (any repair the tight test refuses is printed; must be 0), and the slice below.
   On the sampled Niv no-fire strands ~20-25% had only this guard (the rest also have an untapped Cascade Bluffs or a
   Generator Servant, which stay guards).

Exactness slice for cuts 1+2 (`manifest_exact2.json`: Niv 1,000 pairs both arms + 14 rock decks x 200 on-arm, all ab3 jobs):
**192 of 192 `.wins` byte-identical to ab3**, units 3,720 of 3,720 tests passed. Niv on/off CPU on that slice 1.0659
(ab3 1.0650; nivx after cut 1 1.0695) -- a contended box (load 35-39 on 32 cores), so that ratio is noise-level; the
paired probes above are the clean measurement.

**gi152 (seed 680152), the probe game that went T8 -> unwon with the lever on:** at `--depth 8 --budget-ms 0` BOTH arms
are unwon (off digest d70174a17fc9a5bf, on dc791ba85a50ac1f; ~30 min each). The off arm's budgeted T8 win is not
reachable by the unbudgeted search either, so the lever is not worse there at unlimited budget; the two d8 lines differ
(the rung changes payments), so it is "both arms equal outcome", not "identical game".

Not taken: the **Bluffs** guard cannot be lifted. Counterexample: rest {U}{U}{U}, the board after the bill an Island and
an untapped Cascade Bluffs, the rock an Izzet Signet. The repair feeds the Signet with the Island ({U}{R}) and the Signet's
{R} into the Bluffs ({U}{U}): three {U}. The board alone makes two (Island into the Bluffs), but the tight bound asks it
for {U}{U}{1}. A hybrid filter that makes two of ONE colour lets the rock's off-colour output become an on-colour pair, so
the "one pip per rock colour" bound is not necessary there. A {C}-only rock (Sol Ring) with an untapped board Signet is
likewise unsound ({U}{U} off Island + Signet + Sol Ring: Sol's {C} feeds the Signet and frees the Island), and the
Generator Servant guard is a backtracker coverage gap (the joint backtracker does not crack the Servant, which the
repair's option table does), so those three guards stay.

## USER ruling (2026-11-21, verbatim, relayed by the parent)

"Sounds good. Let's turn it on if the unwon looks okay. We should indeed support this." -- `MTG_LINE_KEEP_ROCK` default
ON is approved on one condition: the Niv probe game that went from a T8 win to unwon with the lever on (gi152, seed
680152) must recover at `--depth 8 --budget-ms 0`; the T6 -> T5 one (gi56) is just a note. Niv's max_turns is 8 (an
unwon game counts 9 in `avg`), so the off arm's budgeted win was on the LAST turn. At d8 b0 BOTH arms are unwon (no
win by T8; digests differ). The lever is therefore not worse than off at unlimited budget, but the game does not
"recover" to the off arm's budgeted T8 -- the unbudgeted search does not find that win on either arm. Whether that
counts as "looks okay" is reported to the USER; GT is not accepted until it is settled.
