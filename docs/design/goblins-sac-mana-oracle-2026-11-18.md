# Goblins: §2b sac-for-mana fodder in the mana oracle's table (2026-11-18)

Status: landed locally (oracle-only; play byte-identical). Lever `MTG_ORACLE_SAC_FODDER`, DEFAULT ON, `=0` restores
the old table. Context: the mana-clean gate `claude-regate-20261118` (src 7e6befeadf4b) had 833 Goblins ORACLE-GAPs
over gi 125000-129999, nearly all Siege-Gang Commander activations or casts paid with no land up.

## 1. Mechanism (verified with a trace)

Repro (old binary, HEAD 8de5d0971): `MTG_MANA_ORACLE=3 MTG_MANA_ORACLE_TURN=2 build/Release/mtg
decks/Goblins/Goblins.cod --profile decks/Goblins/Goblins.profile.json --games 1 --seed 30125026 --game-index 125026`
(run from the repo root).

- T2 main 2 board: Mountain#39(T), Goblin Lackey#12(T), Skirk Prospector#56, Muxus#46, Piledriver#17, Siege-Gang#53,
  three 1/1 Goblin tokens #1000-1002; no untapped land. Plan `{sacoutlet:Siege-Gang}` ("{1}{R}, Sacrifice a Goblin").
- Oracle: `frontier pay1 cost={1}{R} after (no payments)` -> `pay1[(no payment exists)]` -> ORACLE-GAP.
- Engine: all three tokens gone, opponent 19 -> 17. One token is the activation's sacrifice; two were EATEN through
  Skirk Prospector by the engine's §2b payer (`TapForCostSharedOnceImpl` `best_kind == 5`, `LiveSacPayOutlet` /
  `IsSacPayFodder` in `src/core/SpellEffects.h`, the fodder-last block in `TapForCostSharedImpl`) for the {1}{R}.
- Root cause: the oracle's `BuildSources` (ManaOracle.cpp) built its table only from `HumanPreTapOfferFor` on untapped
  permanents. Skirk's outlet is not a pre-tap source (the payer eats bodies, tapped or not), so the oracle's table had
  no fodder at all. **The engine is not wrong here**: legal bodies, legal cost (the activation's source and baked
  victim are excluded through `g_pay_sac_spare`).
- Who shares it: only decks with a §2b sac-for-mana outlet (Goblins = Skirk Prospector, Fungus = Utopia Mycon).
  Dragons' (188) and Jumbo's (91) gate ORACLE-GAPs are different mechanisms (Haven of the Spirit Dragon gy-return;
  Tooth and Nail / Finale / Nylea / Wild Growth).

## 2. The change

- `TurnSolver::PreTap::sac_fodder` (TurnSolver.h): an oracle-only face, "eat this body through the live outlet".
- `ApplyPreTapImpl` (ManaPayment.cpp): a `sac_fodder` branch, the payer's kind-5 path verbatim (live outlet, body by
  number -- tapped is fine --, not `g_pay_sac_victim` / `g_pay_sac_spare`, `IsSacPayFodder`, a colour the outlet
  makes; mark eaten, float the yield, `CommitPaySacSacrifices`). A HUMAN pre-tap refuses it (the viewer offers the
  outlet's own `sacout=` step).
- `BuildSources(..., sac_fodder, sac_victim)` (ManaOracle.cpp): after every real source, one class per `FodderKey`
  (ClassKey + AttachedKey + tapped + temp pump/damage/attacks + haste + KeyedApart) of legal bodies, faces = the
  outlet's colours. Excluded, as the payer's `fodder_ok` excludes them: the cast's sacrifice victim, an activation's
  spares, creature-only mana for a noncreature spell, and an UNTAPPED body the pre-tap offer can tap (the payer taps
  it instead -- a disclosed narrowing: eating a mana dork for the outlet's colour is offered by neither). Outlet
  bodies' classes come LAST, so Build eats Skirk itself after every other body (`SacExpendabilityRank` order, and the
  only order in which such a set pays at all).
- `MinimalPayments(..., bool sac_fodder)` (ManaOracle.h). **Fodder only where the payer eats**: the fodder-free table
  is built first, and the table with fodder only when that one is EMPTY (the payer's fodder-last contract: a body is
  eaten only when the board cannot pay without one). Every minimal option of the fodder table is kept, so an engine
  payment that eats MORE bodies than needed is covered and graded (DOMINATED-*) rather than reported as uncovered.
  Without the fodder-free-first rule, a wide board offered every body subset beside every land: s30126235 T7 wrote
  >470 MB of level-3 trace before it was stopped.
- Only the CHECKS' table holds fodder: `Realize` passes `!t_no_fodder`; `PlanPaymentScript` (the default-OFF
  `MTG_REAL_PAY_SCRIPT`, real play) holds a `NoFodderScope`; the payer's own refine and line keep call
  `MinimalPayments` / `BuildSources` with the default `false`. So play does not move (verified below).

## 3. Results

Unit: 3 new cases in `test/unit/test_mana_oracle.cpp` (table eats Lackey then Skirk, one option; a human pre-tap
refuses a fodder face, an engine pre-tap eats; an SGC shot paid by eating is among the oracle's outcomes). The test
board must set `deck_has_endstep_reanimate` / `deck_has_upkeep_reanimate` false (both default true, and a death that
pays keeps the payer off). Full suite 3547/3547.

Repros (oracle level 2, lever off -> on): ORACLE-GAP 30125026 T2 2 -> 0; 30125157 3 -> 0; 30125177 1 -> 0;
30126235 (T7) 50 -> 0 (DOMINATED-LAND 10 -> 38); 30126275 1 -> 0; 30125255 (`{cast:SGC + sac:Skirk}`) 8 -> 0.

Gate slice, Goblins gi 125000-126999 (2000 games, 80 jobs x 25, one pooled batch per arm, same binary, `MTG_MANA_ORACLE=2`,
32 threads); per-job play digests IDENTICAL between arms:

| class | off | on |
|---|---:|---:|
| ORACLE-GAP | 367 | 5 |
| ENUM-GAP | 18 | 706 |
| APPLY-DROP | 26 | 32 |
| APPLY-DROP-ORDER | 0 | 8 |
| DOMINATED-LAND (not an issue class) | 41 | 93 |
| PHANTOM / DROP-COVERED (counters) | 865 / 313 | 855 / 309 |
| oracle ms (sum) | 70,940 | 621,327 |
| batch wall | 18 s | 111 s |

Cost: the oracle's work rose ~8.8x on this slice (applies 2.7M -> 15.3M), concentrated in a few wide-board games
(SLOW-GAME s30126454 105 s, s30126022 68 s, s30126235 51 s, s30126297 49 s, s30126445 33 s). No CAPPED.

Smoke (`MTG_RSS_CAP_GB=40 bash test/regression.sh --smoke`, Release, dirty src): ALL PASS, 227 passed, configs
changed 0 -- play byte-identical. No GT accepted.

Fungus sanity (Utopia Mycon, gi 125000-125499, 500 games, same protocol): play digests identical; ORACLE-GAP 0 -> 0;
ENUM-GAP 10 -> 129 (119 carry an `(eat)`: the same in-line-outlet menu-gate miss as §5.1, e.g. `{cast:Utopia Mycon +
cast:Saproling Burst}`); DOMINATED-CREATURE 15 -> 16, DOMINATED-LIFE (disclosed) 22 -> 24; oracle ms 884,523 ->
927,693 (+5%); batch wall 208 s -> 207 s.

## 4. Shapes covered

- `{sacoutlet:Siege-Gang}` x1..x4, `{cast:SGC + sacoutlet:SGC}`, `{cast:SGC}`, Krenko + SGC: covered (the bulk).
- `{cast:SGC + sac:Skirk Prospector}` (s30125255 T2): covered (8 -> 0); the later payment after Mogg War Marshal died
  is the payer live again, same mechanism.
- `{land=Three Tree City + cast:Skirk Prospector}` (8 in the off arm): covered.
- `{cast:Goblin Chieftain (+ Krenko) + sacoutlet:Pashalik Mons}` (4): NOT covered -- a different mechanism (Pashalik
  is a death watcher, so the payer is off and no fodder is in either table); see §5.

## 5. Newly surfaced engine misses (reported, not fixed here)

1. **The subset mana gate does not credit an in-line outlet's fodder** (688 of the 706 ENUM-GAPs carry an `(eat)`).
   `AddSacPayFodderToPool` is fed `LiveSacPayOutlet` on the ROOT state (TurnSolver.cpp ~3765, ManaPayment.cpp ~7494,
   ~7547), so a plan that casts Skirk Prospector and then pays a later cast by eating (often Skirk itself or Goblin
   Lackey) is kept off the menu although the payer would pay it. Top shapes: `{cast:Piledriver + cast:Skirk}` (both
   orders, ~97), `{cast:Skirk + cast:Lackey}`, `{cast:Bolt + cast:Skirk}`, `{cast:Skirk + cast:Aether Vial}`.
   Repros (level 2/3): s30125157 gi125157 T2 m1 `{land=Mountain + cast:Skirk + cast:Stingscourger}`; s30125177
   gi125177 T2 m1 `{cast:Lightning Bolt + cast:Skirk}`; s30126275 gi126275 T2/T3 m1 (Skirk > Piledriver > Krenko).
   Whether these lines are ever worth playing is a separate question; the menu cannot reach them at any budget.
2. **APPLY-DROP-ORDER, Chieftain + Siege-Gang + two Skirk** (8): the engine drops `cast Siege-Gang` where the order
   SGC > Chieftain > sac:Skirk x2 pays everything (eating the SGC tokens + Skirk for Chieftain). Repro s30126844
   gi126844 T5 m1.
3. **APPLY-DROP `vial:Goblin Matron`** rose 20 -> 24 (6 of them pay by eating): the engine drops `cast Lightning Bolt`;
   pre-existing class. Repro s30126582 gi126582 T4 m1.
4. Residual ORACLE-GAPs (not fodder): s30125468 gi125468 T4 m1 `{cast:Goblin Chieftain (+ tapfortokens:Krenko) +
   sacoutlet:Pashalik Mons}` -- the engine ends with the Pashalik activation's victim alive and no tokens, Three Tree
   City and both Mountains tapped (looks like a silently unperformed activation; worth a dig); s30125064 gi125064 T4 m2
   `{sacoutlet:SGC x2}` with Three Tree City and no outlet on the board (engine 2 shots, the oracle's nearest a third).

## 6. Limitations (disclosed)

- An engine payment that eats where a fodder-free payment existed is NOT covered (it would show as ORACLE-GAP); the
  payer's contract says that cannot happen, so such a flag is a payer defect.
- Build order is free taps -> fodder -> creature route -> fed sources, so fodder can feed a fed source (Three Tree
  City's `{2},{T}`), but the City then counts the Goblins left after the eating; the reverse is not tried.
- The fodder table is a superset of the payer's k-ladder (every minimal option, not only the fewest bodies), so cost on
  wide boards is real (§3). If the gate's wall matters, the sound next step is a per-frontier dominance fold of fodder
  options, not a narrowing.
