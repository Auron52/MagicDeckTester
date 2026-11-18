# Dragons: the Urza's Incubator APPLY-DROP family (`MTG_LINE_REDUCER_KEEP`), 2026-11-18

## The family

Mana-clean gate `claude-moderate-20261117` (`logs/mana_clean/claude-moderate-20261117/report.md`): Dragons had 583
APPLY-DROP flags (469 depth-1 rollbacks). The leading signatures are all one shape: a main-phase plan
`{cast:<Dragon> + cast:Urza's Incubator}` in which the engine drops the Dragon, while the oracle pays both: the
Incubator off Mind Stone + Gruul Turf (and/or another source), the Dragon -- reduced by Dragonspeaker Shaman and by the
Incubator once it is in play -- off two Mountains.

## Mechanism (verified, not assumed)

Dragons s30081761 gi81761 T5. Board Mountain#32, Mind Stone#29, Mountain#33, Atsushi, Dragonspeaker Shaman, Gruul Turf
(`{T}: Add {R}{G}`, one of each). Hand Glorybringer, Inferno of the Star Mounts, Urza's Incubator, Mountain x2. Plan
`{no land + cast:Inferno + cast:Incubator}`. `MTG_TAPDBG=1`, the oracle's engine apply of that plan:

```
[tapdbg] ORACLE-ENGINE-APPLY T5 plan={no land + cast:Inferno of the Star Mounts + cast:Urza's Incubator} ...
[tapdbg] TFCD cost g3 W0 U0 B0 R0 G0 C0 fl=0/0/0/0/0/0 w0
[tapdbg] tap Mind Stone#29 col=5 ...
[tapdbg] tap Mountain#32 col=3 ...
[tapdbg] tap Mountain#33 col=3 ...
[tapdbg] TFCD -> 1 tapped: #32 #29 #33
[tapdbg] TFCD cost g0 W0 U0 B0 R2 G0 C0 ...
[tapdbg] tap Gruul Turf#13 col=3 ...
[tapdbg] TFCD -> 0 tapped: #32 #29 #33
```

The realisation order is right (the payable-order decider casts the Incubator first) and the Dragon IS priced with
the Incubator's reduction at its cast (`{R}{R}`). The defect is the Incubator's payment: the greedy pays the `{3}` pip
by pip (Mind Stone, then the basics ahead of the Karoo on the ladder) and leaves Gruul Turf's `{R}{G}` for a `{R}{R}`.
Mind Stone + Gruul Turf pay the `{3}` exactly and keep both Mountains. Two things hid it from every line-aware rung:

1. **The line hold over-states the rest of the line.** `g_line_unpaid_cost` (`LineCastCostTotal`) sums each cast's
   `Action::cost`, the price at enumeration, before the Incubator is in play: Inferno at `{2}{R}{R}`, the line at 7
   mana on a 5-mana board. The subset math had afforded the line WITH the same-turn reducer credit
   (`SameTurnReducerGenericCredit`); the hold had not. So no rung could see that the bill and the rest fit together.
   (`ComputeNeedsDemandSupply`'s hand demand also skips Inferno: printed MV 6 > the 2 mana left.)
2. **No per-pip rule can find the payment.** The Karoo's two units must go together on the generic; the needs ladder
   sees a Mountain and the Turf as equal-harm for one `{R}` and ranks the basic first.

A second shape (s30081938 T5, x16; and the depth-2 s30084117): two Gruul Turfs + Mind Stone. The greedy pays the
`{3}` exactly with Mind Stone + one Turf; the keeping payment taps all three, spends `{C}{G}{G}` on the generic and
floats `{R}{R}` for the Dragon -- a SUPERSET of the greedy's taps, which no hold-out can reach.

## The fix (`MTG_LINE_REDUCER_KEEP`, default ON)

* **Seed.** Beside the line hold, both apply worlds (`ApplyPlanDirect`, `AIEngine::TakeTurn`) and the payer walks that
  bind the hold from an action list (`SubsetPayableAsExecutedImpl`, `SubsetPayableSequential`'s walk,
  `OrderPrepaysWholeTurnImpl`) bind `LineReducerCreditScope`: the line's same-turn reducer credit
  (`LineReducerCredit` = the enumerator's own `SameTurnReducerDiscountFor`, summed over the held casts) and the line's
  noncreature cast bill. Every `LineUnpaidCostScope` clears both, so a nested apply / continuation that binds its own
  hold never reads an outer credit (the rung is inert there).
* **Rung** (`LineReducerKeepHold`, `TapForCostSharedCore`, outermost payment, after the existing hold-out rungs). The
  REST of the line is the hold net of this payment, less the credit. If the rest owes a coloured pip, the payment is
  tried on a scratch board; only when that trial leaves the rest unpayable while the board pays bill + rest together:
  1. the same greedy with the FEWEST of its own sources held out (then battlefield order) that keeps the rest payable;
  2. failing that, the first option of the oracle's payment table (`manaoracle::MinimalPayments`, the table
     `MTG_PAY_REFINE` already uses in play; it offers a multi-unit tap plus one more) whose board pays the rest.
  Otherwise the payment is the greedy's, byte for byte. The rest is judged by the complete backtracker, in creature
  mode when every noncreature cast of the line is this payment (Haven of the Spirit Dragon's colour pays a Dragon).
* **Soundness.** The rung replaces a payment only when that payment provably strands the rest of the declared line and
  another payment of the same bill does not. A rest priced too low (a reducer cast after the card it discounts) can only
  make it fire less. Payment-level only (the standing mana-payment exemption of the no-greedy rule): it chooses
  sources for a cast the plan already names; it never picks a plan. Applies at every budget. `=0` is byte-identical
  (the seed returns 0 and the rung is skipped).
* **Lever.** `MTG_LINE_REDUCER_KEEP` (`LineReducerKeepOn`, `src/ai/EngineFlags.h`; heurarm slot `LINE_REDUCER_KEEP`,
  read per call). Diagnostic counters: `MTG_LINE_REDUCER_KEEP_STATS=1` prints `[line-reducer-keep] trials stranded
  fired` at exit.
* **Unit test.** `test/unit/test_line_reducer_keep.cpp`: the s30081761 board (on: both cast; off: Inferno unpayable),
  the s30081938 two-Turf board (the table stage), and two inert controls (no credit; a third Mountain).

## Measurement (binary = this commit, Release; `MTG_RSS_CAP_GB=40`)

### Exact repro seeds (oracle `MTG_MANA_ORACLE=3`, APPLY-DROP flags at the named turn, lever on / off)

| seed / gi | turn | shape | off | on |
|---|---|---|---|---|
| 30081761 / 81761 | T5 | Glorybringer, Inferno + Incubator | 16 | 0 |
| 30082255 / 82255 | T4 | Scourge + Incubator | 8 | 0 |
| 30084491 / 84491 | T5 | Scourge + Incubator | 16 | 0 |
| 30083538 / 83538 | T8 | Lathliss + Incubator | 18 | 0 |
| 30084169 / 84169 | T6 | Lathliss + Incubator | 7 | 0 |
| 30081819 / 81819 | T4 | Atsushi + Incubator | 8 | 0 |
| 30081938 / 81938 | T5 | Inferno + Incubator, two Turfs (table stage) | 8 | 0 |
| 30084117 / 84117 | T6 | Atsushi x2 + Incubator, depth 2 | 16 | 16 |

s30084117 is NOT a reducer case: with Dragonspeaker Shaman in play each Atsushi already costs `{R}{R}`, so the
Incubator discounts nothing (`SameTurnReducerDiscountFor` = 0, credit 0) and the line fits the board without it. It is
the same strand (two Turfs + Mind Stone must pay the `{3}` and float `{R}{R}`) on a line with no reducer credit, which
this lever deliberately does not reach (see Residual).

### Oracle gate slice (Dragons gi 81000-82999, `MTG_MANA_ORACLE=2`, one pooled batch, lever per job)

| arm | APPLY-DROP | of which `cast:Urza's Incubator` plans | ORACLE-GAP | ENUM-GAP |
|---|---|---|---|---|
| off | 211 | 92 | 53 | 0 |
| on | 99 | 2 | 53 | 0 |

`[line-reducer-keep] trials=23767 stranded=1194 fired=1194` over both arms' 4,000 games. The two Incubator plans left
(s30081539 T4, s30082775 T5) need Haven of the Spirit Dragon's creature-only `{R}` for the Dragon on a line whose rest
the rung judges in noncreature mode (a Lightning Bolt in the line / Sol Ring's `{C}{C}`); left as they are.

### Held-out paired A/B (one pooled `mtg --batch`, 1,120 jobs of 25 games, seed = 30600000 + off, gi = 600000 + off)

Manifest `build/scratch/claude_dragons_inc/manifest_ab.json`, per-job pin `{"MTG_LINE_REDUCER_KEEP": true|false}`,
profile attached. Unwon = 9. Decks: Dragons at 5,000 pairs; every other deck that holds a same-turn reducer the credit
can see (Boars: Herald's Horn; Dragonstorm: Ruby Medallion; Giants: Stinkdrinker Daredevil; Goblins: Goblin Warchief;
Hydras: Gargos; Kitty Equipment: Cid; Merfolk Turns: Stonybrook Banneret; Spirits: Patrician Geist) at 1,000; Treasure
Hunt and Angels (no reducer) at 500 as byte-identity controls.

| deck | pairs | faster | slower | net turns (on-off) | digest-only | units on/off |
|---|---|---|---|---|---|---|
| Dragons | 5000 | 0 | 0 | 0 | 29 | 1.0001 |
| Boars | 1000 | 0 | 0 | 0 | 0 | 1.0000 (35,701,556 / 35,703,095) |
| Dragonstorm | 1000 | 0 | 0 | 0 | 0 | 1.0000 |
| Giants | 1000 | 0 | 0 | 0 | 0 | 1.0000 (identical) |
| Goblins | 1000 | 0 | 0 | 0 | 1 | 1.0000 (identical) |
| Hydras | 1000 | 0 | 0 | 0 | 0 | 1.0000 (identical) |
| Kitty Equipment | 1000 | 0 | 0 | 0 | 0 | 1.0000 (identical) |
| Merfolk Turns | 1000 | 0 | 0 | 0 | 2 | 1.0003 |
| Spirits | 1000 | 1 | 0 | -1 | 4 | 0.9987 |
| Treasure Hunt (control) | 500 | 0 | 0 | 0 | 0 | identical |
| Angels (control) | 500 | 0 | 0 | 0 | 0 | identical |

`[line-reducer-keep] trials=2694186 stranded=271984 fired=227845` (on arms, search included). Pooled wall per deck is
within the batch's noise (Dragons on/off 0.99; the byte-identical Angels control read 1.17).

**No slower game anywhere**, so the depth-8 recovery replays have nothing to replay; every deck is net <= 0. The
drops the lever fixes are mostly on plans the search did not take at the shipped setting (the oracle audits every
plan), which is why the win turns do not move; the fix makes those plans realise as scored.

## Residual (not this lever)

The other Dragons APPLY-DROPs on the gate slice are the same greedy strand on lines with NO reducer credit:
`{Scourge/Glorybringer/Atsushi + Mind Stone}`, `{Dragon + Sol Ring}`, `{Tempest + Lathliss + Sol Ring}`, s30084117.
The rung would repair most of them if it fired on every multi-cast line, but then it would run a scratch trial and a
backtracker solve on every line payment of every deck; that is a separate change needing its own cost measurement.
