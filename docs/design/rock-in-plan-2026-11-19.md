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
counts as "looks okay" was then root-caused (below): clear clairvoyance, so GT was accepted.

## gi152 root cause and verdict (agent 5, 2026-11-21): CLEAR CLAIRVOYANCE

Replays (binary bc710d3f3-era `mtg.gi152`; scratch `build/scratch/claude_rock_plan/`):

* **Play settings, with logs** (`g152p_{on,off}/`): reproduced, off T8, on unwon. Identical through T4. They first
  diverge at **T5 main 1** on Spirebluff Canal + Cascade Bluffs + Island + Sol Ring with {Signet, Curiosity, Preordain,
  Remand} in hand. Off: Izzet Signet (Sol Ring) + Curiosity, holding Remand and Preordain. On: Izzet Signet + Preordain +
  Remand on its own Preordain (cantrip, draws the second Signet) + Curiosity. The four-spell line is one the rung makes
  payable (the greedy strands it). The search CHOSE it, and the off arm's two-spell line stays available to it, so the
  lever adds an option and removes none. The draws then differ. Off has Niv-Mizzet, the Firemind with three Curiosities
  by T8 and draws out in combat. On reaches Niv only in T8 main 2 and ends with the opponent at 5.
* **`MTG_NC_SEARCH=1` at play settings** (`logs_nc152/`): **BOTH arms unwon** (off 0f96fefd64562e4f, on
  a091cce042f7233f). The unpruned (lever-off) arm loses its T8 win once it cannot see the library. That is the repo test
  for clear clairvoyance.
* **d8 b0, max_turns 8**: both unwon (agent 4). **d8 b0, max_turns 10** (`logs_mt10/`): **both arms win T9** (off
  4de8cc5a7ff62b58, on 7beeba6db861d178). At unlimited budget the lever's line is exactly as fast as off's.

Verdict: the off arm's budgeted T8 is a clairvoyant budget artefact, absent without clairvoyance and absent at
unlimited budget. The lever does not remove a capability. GT accepted on that basis.

## Default-ON tiers (chain7, full cores, binary da3d3cb61) and accept

Gates: build ok; units rc 0; `MTG_LKR_TIGHT_CHECK=1` Niv 200: 0 `[lkr-tight-bad]`; SIGNET_PAIR 0/1 vs pfx1: 0 diffs;
exactness slice vs ab3: 0 of 192 diffs. Tiers: smoke 13 cases changed, regression 18 (all fingerprint-only or small).

Per-deck net (games x turns, loss = max_turns+1), searched / d0:
smoke -- every searched cell 0 (8 play-changed at same score); d0 hinata -4, galadriel -1, **niv +20**, the rest 0.
regression -- searched: dragons -1, hinata -1, niv -2 (d3 +1, d5 s2002 -3), the rest 0; d0: bruna -3, fivecolour -1,
galadriel -4, niv -6, sea -2, the rest 0.
The one searched SLOWER game, niv_regression_d3_s2002 gi17 (seed 2019) T4 -> T5, **recovers at d8 b0** (T4 on both
arms, identical digest). Niv d0 (the greedy runner, lighter bar) nets +14 over the two tiers (smoke +20 / 1000, regression
-6 / 1000), reported to the USER.

CPU (Signet-pair cut, paired Niv 200, 1 thread): rung 15,034 ms -> 13,833 ms (-8%), table 6,901 -> 6,062 ms,
process CPU 204.3 s -> 200.3 s (-2%), fired identical (126,399). Exact slice at 32 threads: Niv on/off CPU **1.0630**
(units 0.9942). The lever's remaining Niv cost is about +6%, uncut.

## Niv d0 (+20 smoke) root cause and fix: the rung sacrificed Generator Servant (agent 6, 2026-11-21)

The Niv d0 rows moved +20 (smoke, 21 slower / 14 faster of 1000) and -6 (regression). Replays of the two worst smoke
games at d0 (`build/scratch/claude_rock_plan/d0/`, binary da3d3cb61):

* **gi82 (seed 1083) T5 -> unwon.** T3, board Cascade Bluffs + Island + Generator Servant, the line {Izzet Signet,
  Ponder, ...}. Off: the Signet paid off the lands, the Servant attacks (opp 17), Niv-Mizzet T4, win T5. On: the rung's
  oracle table pays the Signet's {2} by CRACKING the Servant ({T}, sacrifice: {C}{C}) so that the rest stays payable;
  the Servant is gone, nothing attacks, and the game never recovers.
* **gi373 (seed 1374) T6 -> unwon.** T4, Curiosity already on the Servant: the rung again cracks the Servant for the
  Signet's {2}; Curiosity falls off with it.

Mechanism: Generator Servant is a DIRECT-ACTION sacrifice source (the SacAction route). Its mana exists in the engine
only through a plan's own SacForMana action, which the search branches on; no payer cracks it mid-payment (the
mana-oracle ENUM-GAP pass already leaves the route out for exactly this reason, `NoSacActionScope`). The rung's repair
table (`manaoracle::MinimalPayments`) did NOT leave it out, so a repair could sacrifice a creature that no search had
decided to sacrifice -- a resource the line's own later casts (Curiosity's host) and combat needed.

Fix (`MTG_LKR_NO_SAC_ACTION`, default ON): the rung's table is built under `manaoracle::NoSacActionRouteScope`, so a
repair never cracks a direct-action sacrifice source. Consequence for the tight joint test: such a source is then no
longer a guard (nothing the rung pays with can crack it), so the Generator Servant guard is lifted -- the bulk of
Niv's no-fire strands (the stats sample) had only that guard. Pay-sac (Treasure) and on-demand (Lotus Bloom, Lake of
the Dead) sources are cracked by the payer itself and stay guards. Only Niv-Mizzet Combo holds a direct-action
sacrifice source among the suite's rock decks.

## Current per-deck CPU of the rung, 2026-11-22 (agent rock-cpu, step 1 of the USER-approved "Rock Niv" plan)

USER ruling (verbatim, relayed): "Sure, we can do your recommendation on Rock Niv." -- a fresh per-deck CPU measurement
first, then a search for a cut that gives up some quality. And: "I'm mostly concerned with searched."

Binary: origin/phase-1-2-deck-analyzer 4a7f65983 (HEAD:src 104f1245), Release, `MTG_PGO=0`. One pooled batch on the box
lock, both arms interleaved (`MTG_LINE_KEEP_ROCK=1` vs `=0`), 32 threads, 32/32 busy. Seeds: searched base 700000 (play
settings), d0 base 710000. Scratch: `build/scratch/claude_rock_cpu/` (`m1.json`, `m1.out`, `logs_m1/`, `m1.cmp.txt`).
Units are budget units (d0 charges none). CPU = per-job thread CPU on a shared box (load ~30), so ratios under ~1% are
noise, and the d0 CPU column is mostly noise (d0 games cost ~10 M instructions each). Instruction counts (perf stat,
user space, 1 thread, 12 searched / 50 d0 games per deck, seeds 740000 / 750000, process start-up included, so cheap
decks' ratios are diluted) are the deterministic check.

| deck | searched pairs | faster | slower | net turns | units on/off | CPU on/off | instr on/off (12 g) | d0 pairs | d0 faster / slower | d0 net | d0 CPU on/off | d0 instr on/off (50 g) |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| angels | 1000 | 0 | 0 | 0 | 1.0000 | 1.0025 | 1.0082 | 1000 | 0/0 | 0 | 1.0537 | 1.0002 |
| bruna | 1000 | 1 | 0 | -1 | 0.9981 | 1.0034 | 1.0065 | 1000 | 1/1 | +1 | 1.0878 | 1.0002 |
| dragons | 1000 | 0 | 0 | 0 | 0.9996 | 1.0089 | 1.0185 | 1000 | 3/0 | -3 | 1.0641 | 0.9995 |
| extraturns | 1000 | 0 | 0 | 0 | 1.0000 | **1.0329** | **1.0667** | 1000 | 0/0 | 0 | 1.0130 | 1.0081 |
| fivecolour | 1000 | 0 | 0 | 0 | 1.0001 | 1.0073 | 1.0149 | 1000 | 2/1 | -1 | 1.3764 | 1.0006 |
| fungusv3 | 1000 | 0 | 0 | 0 | 1.0000 | 1.0015 | 1.0117 | 1000 | 0/0 | 0 | 1.0155 | 1.0003 |
| galadriel | 1000 | 1 | 0 | -1 | 0.9998 | 0.9991 | 1.0237 | 1000 | 3/0 | -4 | 1.0244 | 1.0031 |
| giants | 1000 | 0 | 0 | 0 | 1.0000 | 1.0053 | 1.0057 | 1000 | 0/0 | 0 | 1.0281 | 1.0001 |
| hinata | 1000 | 1 | 0 | -1 | 1.0002 | **1.0285** | **1.0287** | 1000 | 1/0 | -1 | 1.0348 | 1.0012 |
| kittyv3 | 1000 | 0 | 0 | 0 | 1.0000 | 1.0008 | 1.0016 | 1000 | 0/0 | 0 | 1.0053 | 1.0000 |
| merfolkturns | 1000 | 0 | 0 | 0 | 1.0000 | 1.0105 | 1.0015 | 1000 | 0/0 | 0 | 1.0959 | 1.0001 |
| niv | 2000 | 10 | 4 | -6 | 0.9998 | **1.0641** | **1.0652** | 1000 | 10/3 | -12 | 1.0267 | 1.0013 |
| sea | 1000 | 1 | 0 | -1 | 0.9992 | 1.0004 | 1.0025 | 1000 | 0/0 | 0 | 1.0240 | 1.0004 |
| stompy | 1000 | 0 | 0 | 0 | 1.0000 | 1.0182 | 1.0074 | 1000 | 0/0 | 0 | 1.0722 | 1.0005 |
| stuffy | 1000 | 0 | 0 | 0 | 1.0001 | 1.0129 | 1.0226 | 1000 | 0/0 | 0 | 0.9695 | 1.0009 |
| whiteknights | 1000 | 0 | 0 | 0 | 1.0000 | 1.0062 | 1.0086 | 1000 | 0/0 | 0 | 1.0114 | 1.0002 |
| ALL | 17000 | 14 | 4 | **-10** | 0.9999 | 1.0256 | 1.0372 | 16000 | 20/5 | **-20** | (noise) | 1.0015 |

Reading: the rung costs real CPU only in searched play, and there in three decks: Niv-Mizzet Combo (+6.4% CPU / +6.5%
instructions), Extra Turns (+3.3% / +6.7%) and Hinata (+2.9% / +2.9%); Dragons, Galadriel, Stuffy, Stompy, Five Colour
are +1-2%, the rest under 1%. Units are unchanged everywhere (the rung is not charged to the budget), so the whole cost
is per-unit. At d0 it is ~0.1% of instructions on every deck. Quality on these fresh seeds: searched net -10 (14 faster,
4 slower, all slower games Niv: gi 79, 1250, 1494, 1527), d0 net -20 (slower: bruna 772, fivecolour 686, niv 74 / 201 /
401). Those 9 slower games were replayed at `--depth 8 --budget-ms 0` replay (`d8_m1.json`, both arms).
**All 9 recover**: at d8 b0 the on and off arms win on the same turn in every one (niv s gi 79 T5/T5, 1250 T4/T4, 1494
T4/T4, 1527 T6/T6; d0 bruna 772 T4/T4, fivecolour 686 T5/T5, niv 74 T4/T4, 201 T4/T4, 401 T5/T5). None hit the unit cap
(the longest, niv s gi 1527, ran ~23-26 min and 8.3-8.6 M units per arm). `logs_d8_m1/`, `d8_m1.out`.

**Larger instruction slice (perf_base100, base binary, 1 thread, seeds 760000 / 770000, start-up baseline of 416 M
instructions per process subtracted).** On/off instruction ratio, searched 100 games / d0 300 games per deck:

| deck | searched | d0 | | deck | searched | d0 |
|---|---|---|---|---|---|---|
| niv | **1.0477** | **1.0409** | | stompy | 1.0088 | 1.0022 |
| extraturns | **1.0391** | 1.0116 | | angels | 1.0076 | 1.0016 |
| hinata | **1.0235** | **1.0376** | | stuffy | 1.0067 | 1.0031 |
| dragons | 1.0173 | 1.0027 | | sea | 1.0046 | 1.0008 |
| galadriel | 1.0157 | 1.0100 | | giants | 1.0045 | 1.0007 |
| whiteknights | 1.0108 | 1.0017 | | merfolkturns | 1.0023 | 1.0009 |
| fungusv3 | 1.0100 | 1.0013 | | fivecolour | 1.0006 | 1.0020 |
| bruna | 0.9992 | 1.0020 | | kittyv3 | 1.0000 | 1.0000 |
| **ALL** | **1.0203** | **1.0090** | | | | |

With start-up subtracted, d0 is not ~0.1% everywhere: Niv +4.1% and Hinata +3.8% of d0 instructions are the rung (in the
d0 runner / executor, which no playout cut or budget price reaches). d0 games are ~2-13 M instructions each, so d0 is
still negligible in absolute terms. Files: `build/scratch/claude_rock_cpu/perf_base100/`.

### Lossy / priced cuts built (wip/rock-cpu, all DEFAULT OFF at the time, not yet measured)

*(2026-11-24: only `MTG_LKR_PLAYOUT_OFF` survives -- ADOPTED DEFAULT ON, see "ADOPTED" at the end. `MTG_LKR_PLAYOUT_NO_TABLE`,
`MTG_LKR_PRICE` / `_PRICE_MILLI` / `_PRICE_TABLE`, the `lkr_rung` unit site and the `[line-keep-ctx]` instrument were
never landed: they were DELETED as failed experiments ("delete, don't lever"); their measurements below stay as a
documented negative. The adopted build prints only `[line-keep-cut] playout_off=N` under MTG_LINE_REDUCER_KEEP_STATS.)*

In `LineReducerKeepHold` (ManaPayment.cpp), applying only to a line with no reducer credit (the rock rung):
* `MTG_LKR_PLAYOUT_OFF` -- no rung in a horizon playout (`TurnSolver::InRolloutPlayout`, beyond the search window, where
  the doctrine permits a greedy policy). Window, executor and d0 keep the full rung.
* `MTG_LKR_PLAYOUT_NO_TABLE` -- in a horizon playout, hold-out sets only, no oracle table.
* `MTG_LKR_PRICE` (+ `MTG_LKR_PRICE_MILLI`, default 1000 milli-units per work event; `MTG_LKR_PRICE_TABLE`, default 4
  events per table ask) -- the rung's work is charged to `TurnSolver::DecisionBudget()` (new unit site `lkr_rung`,
  `TurnSolver::ChargeLineKeepUnits`). nullptr at an unlimited root and with no decision frame, so it is inert at
  `--budget-ms 0`, in the executor and at d0: the preferred budget-priced shape.
* `MTG_LINE_REDUCER_KEEP_STATS=1` now also prints `[line-keep-ctx]` (trials / ms / work / tables / fired split by
  executor / window / playout / d0) and `[line-keep-cut]` (units charged, cuts taken) -- needed to calibrate the price.

### Step 2-3: dev binary identity and price calibration (agent rock-cpu 2, 2026-11-22)

**Identity.** `mtg.dev` (wip/rock-cpu b9013484b, every new flag at its default OFF) replayed the first chunk of every
deck x mode x arm of `m1` (64 jobs: 25 searched / 100 d0 games each, both `MTG_LINE_KEEP_ROCK` arms): **64 of 64 jobs
byte-identical** to `mtg.base` (games, avg, digest, units). `build/scratch/claude_rock_cpu/ab2/ident.txt`.

**Where the rung's time goes** (`MTG_LINE_REDUCER_KEEP_STATS=1`, dev binary, default flags, 1 thread, seeds 795000):

| deck | games | rung % of CPU | executor ms | window ms | playout ms | d0 ms | ns / work event | ns / budget unit | MILLI at 1x |
|---|---|---|---|---|---|---|---|---|---|
| niv | 60 | 4.5% | 0.5 | 310 | **2,243** | 0 | 3,730 | 44,010 | 85 |
| extraturns | 40 | 4.2% | 1.0 | **2,280** | 73 | 0 | 10,470 | 120,920 | 87 |
| hinata | 20 | 1.4% | 0.2 | **760** | 126 | 0 | 2,976 | 37,681 | 79 |

So the playout cuts can only reach Niv (88% of its rung time is in horizon playouts); on Extra Turns and Hinata the rung
runs in the search window (Extra Turns: 107 K window trials, two work events each, no oracle table ever -- its trials end
at the witness), where only the price applies. The price was calibrated so that the units charged equal the rung's
window + playout time at each deck's own ns per unit: MILLI = 1000 x (ns per work event) / (ns per unit), median **85**
(1x, ~970 units per Niv game, ~4.5% of its units); the half price is **43**. `ab2/calib.txt`, `ab2/stats_*.err`.

### Step 3: held-out A/B of the cuts, batch A (seeds searched 780000, d0 790000; one pooled batch, 32/32 busy)

Arms vs `def` (dev binary, default flags; rock rung ON). Searched = play settings. CPU = per-job thread CPU on a shared
box (noise ~1-2%). net = turns, unwon = max_turns + 1. Full tables: `logs/rock-cpu/cmp_{poff,pnt,price}.txt`.

| deck | pairs | poff f/s | poff net | poff CPU | pnt f/s | pnt net | pnt CPU | price@85 f/s | price net | price units | price CPU |
|---|---|---|---|---|---|---|---|---|---|---|---|
| niv | 3000 | 10/3 | **-8** | **0.9493** | 3/1 | -3 | 0.9731 | 0/13 | **+13** | 1.1037 | 0.9806 |
| extraturns | 1500 | 0/0 | 0 | 1.0006 | 0/0 | 0 | 0.9961 | 0/0 | 0 (0 digest diffs) | 1.1083 | **0.8759** |
| hinata | 1500 | 0/1 | +1 | 0.9795 | 0/1 | +1 | 0.9826 | 4/10 | **+5** | 1.3890 | **1.1982** |
| dragons | 500 | 0/0 | 0 | 0.9785 | 0/0 | 0 | 0.9773 | 0/0 | 0 | 1.0700 | 0.9969 |
| fivecolour | 500 | 0/0 | 0 | 1.0026 | 0/0 | 0 | 0.9942 | 0/0 | 0 | 1.0348 | 1.0144 |
| galadriel | 500 | 0/0 | 0 | 1.0073 | 0/0 | 0 | 1.0099 | 0/0 | 0 | 1.0388 | 1.0230 |
| merfolkturns | 500 | 0/0 | 0 | 0.9926 | 0/0 | 0 | 1.0005 | 0/0 | 0 | 1.0100 | 0.9978 |
| stompy | 500 | 0/0 | 0 | 0.9885 | 0/0 | 0 | 0.9899 | 0/0 | 0 | 1.0294 | 0.9841 |
| stuffy | 500 | 0/0 | 0 | 0.9915 | 0/0 | 0 | 0.9962 | 0/0 | 0 | 1.0046 | 0.9421 |
| **ALL** | 9000 | 10/4 | **-7** | 0.9772 | 3/2 | -2 | 0.9842 | 4/23 | **+18** | 1.2079 | 1.0608 |

d0 (500 games per deck per arm): **every arm byte-identical to def** (0 faster, 0 slower, 0 digest-only), as built.
Slower searched games: poff hinata 975, niv 352 / 871 / 1346; pnt hinata 975, niv 871; price 10 hinata + 13 niv (list in
`cmp_price.txt`). d8 b0 replays queued (`build/scratch/claude_rock_cpu/ab2/d8_ab2.json`, lane, marker `d8_ab2.done`): every
poff / pnt slower game on both arms, plus two price games as a check that the price is inert at b0.

### Batch B (price at MILLI 43) and instruction counts

price05 (MILLI 43, same seeds) vs def, searched: niv 0/13 **+13**, hinata 4/10 **+5**, every other deck 0; units ratio
ALL 1.2026 (price@85: 1.2079); d0 byte-identical. **The half price is effectively the same arm as the full price, and
the reason is a defect in the price as built:** the charge is rounded UP per call (`(work * MILLI + 999) / 1000`) and
never carried between calls, and a call is only 2-4 work events, so at any MILLI below ~250 every call costs exactly 1
unit. The arms really charged ~1 unit per rung call: about 3x the calibrated price on Niv (~3,000 instead of ~970
units per game) and up to ~6x on Extra Turns. So the price at 1x and 0.5x is NOT measured yet. A fix that keeps it
deterministic: carry the fractional residual on the decision frame (`DecisionBudget()`, reset when the frame opens),
not on the thread. Then re-measure at 1x and 0.5x.

User-space instructions per (deck, arm), 1 thread, searched seeds 796000, start-up subtracted, ratio vs def (every
sampled game had the same outcome and digest as def, so these ratios are per-unit savings):

| deck | games | poff | pnt | price@85 | price@43 | def G instr / game |
|---|---|---|---|---|---|---|
| niv | 40 | **0.9550** | **0.9539** | **0.9387** | 0.9417 | 4.44 |
| extraturns | 30 | 0.9980 | 1.0002 | 0.9913 | 0.9915 | 7.65 |
| hinata | 15 | 0.9828 | 0.9874 | 1.0000 | 1.0009 | 14.98 |
| dragons | 30 | 0.9993 | 1.0000 | 1.0241 | 1.0242 | 0.45 |
| galadriel | 30 | 0.9976 | 1.0000 | 0.9640 | 0.9640 | 0.91 |
| fivecolour | 30 | 1.0000 | 1.0000 | 1.0000 | 1.0000 | 2.78 |
| stuffy | 30 | 0.9923 | 1.0000 | 0.9708 | 0.9708 | 2.66 |
| stompy | 30 | 0.9989 | 1.0000 | 1.0001 | 1.0001 | 0.36 |
| merfolkturns | 30 | 0.9956 | 0.9998 | 0.9998 | 0.9998 | 3.45 |

The batch's thread-CPU ratios for the price arm (extraturns 0.876, hinata 1.198) do not agree with these counts
(0.991, 1.000), so I read them as shared-box timing noise, not as a cost. On Extra Turns the price left all 1,500 games
byte-identical (the same digests), so it can only have trimmed search that did not change any choice. `ab2/perf/`.

### d8 b0 replays of the A/B's slower games (lane, `ab2/d8_ab2.json`, `logs_d8_ab2/`)

**Every one recovers, byte-identically**: at `--depth 8 --budget-ms 0` each cut arm plays the SAME game as def (same
win turn, same digest): poff niv 352 T4, 871 T5, 1346 T4, hinata 975 T5; pnt niv 871 T5, hinata 975 T5; and the
two price checks (niv 360 T4, hinata 141 T4: the price is inert at b0, as built). None hit the unit cap (the longest,
niv 871, ran ~3.4 M units and ~7-9 min per arm). The 23 per-call-price slower games recover by construction (the price
never reads a budget at b0); only the two above were replayed as a check.

### The price with its residual carried (commit e578605d1; `mtg.dev2`, flags-off identity vs base 64/64)

Searched only, same seeds as batch A, vs batch A's def (`logs/rock-cpu/cmp_pricec*.txt`):

| deck | pairs | price@85 f/s, net | units | CPU | instr (30-40 g) | price@43 f/s, net | units | CPU | instr |
|---|---|---|---|---|---|---|---|---|---|
| niv | 3000 | 0/3, **+3** | 1.0356 | 1.0101 | 1.0027 | 0/1, **+1** | 1.0196 | 1.0045 | 1.0078 |
| hinata | 1500 | 2/5, **+3** | 1.1044 | 1.1164 | 1.0018 | 1/5, **+4** | 1.0558 | 1.1709 | 1.0026 |
| extraturns | 1500 | 0/0, 0 | 1.0238 | 0.9192 | 0.9999 | 0/0, 0 | 1.0125 | 0.9383 | 1.0000 |
| other 6 decks | 3000 | 0/0, 0 | 1.00-1.01 | noise | 0.97-1.00 | 0/0, 0 | 1.00-1.01 | noise | 0.999-1.000 |

At the calibrated price, the charge mostly adds units on top (units ratio > 1). Most decisions finish inside their
budget, so they never bind and the price trims nothing. **It saves no instructions** on the decks that carry the cost,
and it makes Niv and Hinata slower. Hinata's thread CPU rose 12-20% in all four price arms while its instruction count
did not move on 15 games. I have not explained that. The price is the right SHAPE (inert at b0), but it is the wrong
lever here, because the rung's cost sits mostly in decisions whose budget does not bind.

## THE MENU (agent rock-cpu 2, 2026-11-22). Nothing adopted; every flag stays default OFF; the USER decides.

Rung cost to cut (step 1 + perf_base100, searched): Niv +4.8% instructions / +6.4% CPU, Extra Turns +3.9% / +3.3%,
Hinata +2.4% / +2.9%, every other deck <= ~1.7%. At d0 the rung is Niv +4.1% and Hinata +3.8% of instructions, but
d0 games cost only ~2-13 M instructions each, and no cut here reaches the d0 runner.

| cut | shape | Niv searched (3000) | Hinata (1500) | Extra Turns (1500) | other 6 decks (3000) | d0 | slower games at d8 b0 |
|---|---|---|---|---|---|---|---|
| **MTG_LKR_PLAYOUT_OFF** | predicate in the horizon playout (beyond the window: the doctrine's greedy policy) | **CPU -5.1%, instr -4.5%**; net **-8** (10 faster / 3 slower) | CPU -2.1%, instr -1.7%; +1 (0/1) | ~0 / ~0; 0 | ~0; 0 | byte-identical | all 4 recover (identical game) |
| MTG_LKR_PLAYOUT_NO_TABLE | same place; hold-out sets only | CPU -2.7%, instr -4.6%; -3 (3/1) | CPU -1.7%, instr -1.3%; +1 (0/1, the same game) | ~0; 0 | ~0; 0 | byte-identical | both recover |
| MTG_LKR_PRICE (per-call round-up, as first built) | budget price | instr -6.1%; **+13** (0/13) | +5 (4/10), CPU +20% | instr -0.9%; 0 | 0 | byte-identical | inert at b0 (2 checked) |
| MTG_LKR_PRICE (carried residual, 1x / 0.5x) | budget price, inert at b0 | instr +0.3% / +0.8%; +3 / +1 | +3 / +4 | 0 / 0 | 0 | (searched only) | inert at b0 |

**Recommendation: `MTG_LKR_PLAYOUT_OFF`**, with the default left to the USER. It removes almost all of Niv's rung cost
(about 4.5-5% of Niv's searched CPU), and about 2% on Hinata. On the held-out sample its quality is BETTER, not worse:
net -7 over 9,000 pairs. Every slower game replays identically at d8 b0, and d0 is byte-identical. It cannot help Extra
Turns, whose rung time is ~97% in the search window. The trade-off to weigh: it is a predicate (in the beyond-horizon
playout, where the doctrine permits a greedy policy, never inside the search window), so it changes play at every
budget, including b0. It is not a budget price. Adopting it would need GT accepts as a play change. `NO_TABLE` is
dominated by `PLAYOUT_OFF`. The price, in both forms, saves nothing and costs quality: drop it (or keep it OFF as a
documented negative).
Extra Turns' window cost (3-4%) and Hinata's remain. No cut here reaches them without cutting the in-window rung, which
is the play the rung exists for. The next candidate there is a per-unit (exact) speedup of the window trials: Extra
Turns averages 2 work events per trial and never reaches the table, so its cost is the trial payment plus the witness.

### Confirmatory re-measure of MTG_LKR_PLAYOUT_OFF (fresh seeds; one pooled batch, 32/32 busy; `mtg.dev2`)

Parent's brief: Hinata failed the every-deck bar only by +1 (one slower game, which recovers), so re-measure Hinata at
4,000 pairs on fresh seeds, plus Niv 2HG and Extra Turns 2HG at 1,000 pairs. `build/scratch/claude_rock_cpu/hc/`,
`logs/rock-cpu/cmp_hc_poff.txt`. No game unwon on either arm.

| shape | seeds | pairs | faster / slower | net | digest-only | CPU | instr (perf, 12-20 g, start-up subtracted) |
|---|---|---|---|---|---|---|---|
| hinata, play settings (d5 b30) | 800000 | 4000 | 0 / 0 | **0** | 1 | 0.9937 | 0.9989 (same games) |
| hinata d3 b10 | 805000 | 1000 | 1 / 1 | **0** | 3 | 0.9880 | 1.0223 (games differ) |
| niv 2HG d5 b20 | 810000 | 1000 | 2 / 1 | **-1** | 14 | **0.9429** | **0.8862** (same games) |
| extraturns 2HG d5 b20 | 812000 | 1000 | 0 / 0 | **0** | 0 | 0.9986 | 0.9960 (same games) |

Slower games, queued for d8 b0 (lane, `hc/d8.json`, marker `d8_hc.done`): hinata d3 gi 146 (T6 -> T7), niv 2HG
gi 522 (T4 -> T5).
**Both recover, byte-identically** at d8 b0 (same win turn and digest on both arms): hinata d3 gi 146 T6/T6, niv 2HG
gi 522 T4/T4. So `MTG_LKR_PLAYOUT_OFF` now meets the every-deck bar on every shape measured. Searched net: Niv -8,
Niv 2HG -1, Hinata (play) 0 at 4,000 fresh pairs, Hinata d3 0, Hinata at 780000 +1 (recovers), Extra Turns and
Extra Turns 2HG 0, the six +1-2% decks 0. d0 is byte-identical. Every slower game recovers at d8 b0.

## ADOPTED: `MTG_LKR_PLAYOUT_OFF` DEFAULT ON (USER 2026-11-24)

USER ruling, verbatim (relayed by the parent, answering item 3 of the menu): **"3. Okay, let's adopt it."**

Status: **ADOPTED DEFAULT ON 2026-11-24** (branch `wip/lkr-adopt`, rebuilt on origin `e8a2f7d64`). `EnvOn("MTG_LKR_PLAYOUT_OFF",
true)` + heurarm slot `LKR_PLAYOUT_OFF` (per-job arm); `=0` runs the rung inside horizon playouts again. Pinned by
`test/unit/test_line_keep_rock.cpp` ("MTG_LKR_PLAYOUT_OFF (default ON)"), through the new seam
`TurnSolver::RolloutNestAdjustForTest`. The dominated / failed siblings (`MTG_LKR_PLAYOUT_NO_TABLE`, `MTG_LKR_PRICE`,
`MTG_LKR_PRICE_MILLI`, `MTG_LKR_PRICE_TABLE`) were deleted, not landed. The measurements it was adopted on: Niv searched
CPU -5.1% net -8 (3,000 pairs), Niv 2HG CPU -5.7% net -1 (1,000), Hinata play net 0 at 4,000 fresh pairs, Hinata d3 0,
Extra Turns / Extra Turns 2HG / the six small-cost decks 0, d0 byte-identical, every slower game recovers at d8 b0.

