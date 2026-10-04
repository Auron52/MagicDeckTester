# Snow: where did the every-turn second main's extra gain come from?

Status: **RESOLVED (2026-10-04)** -- all four main-1 gaps closed; see "Resolved 2026-10-04". The two games left open by 9380c934 (s3003 gi5, smoke gi69) are root-caused and fixed at the end.

## Background

Snow plays the MDFC Jorn, God of Winter // Kaldring, the Rimestaff as **Jorn** (USER 2026-10-03).
Jorn's attack trigger untaps every snow permanent, so the deck needs a post-combat main on the
turns Jorn attacks. Two shapes were measured on all Snow tier cases (4,330 games), against the
3de66a1f baseline:

| arm | searched units | avg win turn, searched cells |
|---|---:|---|
| Skred never cast (`NeverCast`), no Jorn | 0.85x | ≈ baseline (±0.02) |
| Jorn, main 2 on **every** turn (`DeckUsesSecondMain`) | 1.66x | 0.05–0.23 better |
| Jorn, main 2 **only on Jorn turns** (`UntapSecondMainLive`) — shipped | 1.30x | 0.03–0.20 better |

The shipped arm keeps most of the gain. The remainder — the every-turn arm is a few hundredths of
a turn better in most cells — comes from playing a main 2 on turns Jorn did **not** attack.

Caveat on the every-turn row: it was measured on a binary with the untap-timing bug (attackers were
tapped by `ResolveCombatDamage` after Jorn's untap), so its Jorn turns were slightly *under*-served.
The gap is therefore, if anything, understated.

## The open question

What does a non-Jorn main 2 buy a deck with no post-combat effects? Every Snow card is at least as
good cast in main 1 (snow permanents feed the Treefolk before combat), so a main-2 cast should be
weakly dominated — unless main 1 **could not** make the play. Candidates:

1. A card main 1 could not see: something found or drawn mid-turn after the main-1 search committed.
2. A tie broken the wrong way. The first suspect, s2002 gi56 T4, turned out to be this and harmless:
   the root searched "cast the Scrying Sheets find (Arcum's Astrolabe) now" and the base line tied
   on win turn (both verified T5), so the base line won the tie. Not a gap.
3. Budget: at d3/d5 b10 the main-1 search may truncate before reaching a line that a cheap main 2
   then picks up.

Only (1) or (3) would be a real defect; (2) is a tie-break preference.

## Method (about 30 minutes)

1. Run Snow's three tiers (`--deck=snow`) on both binaries, keeping each run's
   `test/logs/<mode>/wins` (they are overwritten by the next run — copy them out).
   Every-turn arm: rebuild d08c2c6f (Jorn + `DeckUsesSecondMain`), ideally with the untap-timing fix
   from 6fb10c80 cherry-picked so the arms differ only in the main-2 gate.
2. Diff per game; take the games faster **only** in the every-turn arm.
3. For a few of them: `MTG_FD_TRACE=1 MTG_BP_TRACE=1 MTG_BP_CONT_TRACE=<turn>` on both binaries,
   find the non-Jorn turn whose main 2 acted, and classify it as (1), (2) or (3).
4. Re-run those games at `--depth 8 --budget-ms 0` on the shipped binary: if they reach the
   every-turn arm's turn there, it is budget (3), not expressiveness.

## Measured 2026-10-04

Both arms in ONE pooled batch on f5ba78cc (every-turn = the per-job flag
`"flags": {"MTG_FORCE_USES_M2": true}`, which restores the deck-wide second main on the same
binary -- so the arms differ ONLY in the main-2 gate, both with the untap-timing fix):

* Control: the shipped (per-turn) arm reproduced Snow's accepted GT exactly, 0 / 4,330 per-game
  mismatches, inside a 24-worker pooled batch.
* Every-turn vs per-turn, 3,600 searched games: net **-15** (21 faster, 6 slower), **1.33x** units.
* The 15 distinct faster games re-run at `--depth 8 --budget-ms 0` on the shipped binary:
  * **10 reach the every-turn result** -> budget churn, not a gap (smoke s1001 gi12/gi78, regression
    s2002 gi8/gi15, s3003 gi10, overnight s5005 gi25/gi149, s6006 gi10/gi143, s7007 gi38).
  * **4 do NOT** -> a real gap: overnight s5005 gi147 (d8b0 8 vs 7), s4004 gi110 (7 vs 6),
    s4004 gi28 (7 vs 6), s6006 gi33 (7 vs 6).
  * **smoke s1001 gi2 is an anomaly of its own**: d3/b10 per-turn wins T7, but d8b0 on the same
    binary gives **T8** -- an unlimited search doing worse than a budgeted one. Investigate separately.

### The gi147 repro (cheapest: 0.4 s at d8b0)

`logs/m2q/g147.json`-style manifest: deck Snow, seed 5152, game_index 147, depth 8, budget_ms 0,
ignore_play_profile; second job identical plus `"flags": {"MTG_FORCE_USES_M2": true}`.

* Every-turn arm commits at T1 a verified **T7** line whose T6 is:
  m1 `Ice-Fang Coatl | bp[k4:Scrying Sheets]` (Coatl's ETB draws Scrying Sheets; the continuation
  plays it), m2 `Scrying Sheets(x1)` (the look; it finds Frost Augur, cast T7).
* Per-turn arm commits at T1 a verified **T8** line; its T6 is an empty phase (`land=(defer)`), it
  attacks for 5 instead of 7, and holds Coatl.
* The main-1-only equivalent IS offered: inside the T1 search, the T6 site-10 continuation list for
  Coatl's draw contains `#0 land=Scrying Sheets: Scrying Sheets(x1)` (play the drawn Sheets and look
  in the same continuation), and follow-on lists show the look's find (Frost Augur). Mana suffices:
  Coatl on Forest+Island, the look's {1}{S} on Island+Mountain, Sheets' own {T}.

So the T7 line is EXPRESSIBLE in main 1 and an exhaustive search still settles on T8. Something on the
main-1 path drops or misprices that variant -- candidates, in the order to check: (a) the site-8 look
NESTED inside a site-10 continuation resolving narrow / EMPTY at scoring time (the apply's
`window_base` branch), so the find never reaches hand in the scored world; (b) a dominance or
dedup collapse merging the look variant with the no-look one; (c) order-condemnation.

Next step: `MTG_BP_TRACE=1 MTG_BP_CONT_TRACE=6` on the per-turn job, find the T6 node that applies
continuation #0 and dump its post-apply hand (does Frost Augur arrive?) and its scored win turn.

## Resolved 2026-10-04

### The defect: a continuation's board activations were never applied (gi147, gi33, smoke gi2)

`MTG_FSW_TRACE` (with the new `[fsw-skip]` lines -- one per plan the frontier loop declines before
its own `[fsw]` print) showed the T6 node the committed line passes through scoring the Coatl
continuation variants #0 and #1 as the SAME state: `cont=[k4:Scrying Sheets]`, land only, and #0
skipped as a post-apply duplicate. The continuation list is built by `CollectActions`, so it offers
board activations ("play the drawn Sheets and look"), but every continuation site except 7 and 9
applied it through the cast-only `apply_plan_actions` -- and the executor's two continuation paths
(`resolve_draw_breakpoint`, `replay_recorded`) likewise ran casts only. Lockstep, so no
`[fd-diverge]` ever fired; the entry was simply inexpressible at any budget. Main 2 "fixed" it by
running the look as an ordinary plan action.

Fix (both worlds): `apply_continuation_activations` runs a continuation's trailing activations after
its casts and records them into the breakpoint script; the executor dispatches them at the same
point in `resolve_draw_breakpoint` and as one trailing pass in `replay_recorded`. Two definitions
moved so the dispatchers exist when first reached (a lambda definition executes nothing): the
rollout's `apply_trailing_activations` ahead of the `bp_resume` branch (a node-resumed apply skips
the main-phase block), the executor's `exec_trailing_activations` ahead of the main cast loop.

At d8b0, shipped (per-turn) arm: gi147 8 -> **7**, s6006 gi33 7 -> **6**, smoke s1001 gi2 8 -> **6**
(the "unlimited worse than budgeted" anomaly was the same defect).

### s4004 gi110: not a gap

With the fix, the every-turn arm at d8b0 realises 7 too -- the 6 it showed was at d3/b10 and no
unlimited search reproduces it in either arm.

### s4004 gi28: the cast order lost Slumber's scry -- changed (USER 2026-10-04)

The every-turn line's T3 is m1 `Forest; Marit Lage's Slumber`, m2 `Boreal Druid` (tail 6). Main 1
`Slumber, Druid` scored 9 in BOTH arms: Snow ships the GENERIC order (MTG_SNOW_CAST_ORDER was measured
worse and stays off), which ranks a creature (10) and a rock (5) ahead of an enchantment (20), so in one
phase the Druid and Coldsteel Heart always entered before Slumber and its "another snow permanent
enters -> scry 1" never saw them. Splitting the casts across mains was the only way to buy the scry.

USER: *"we should change that. Boreal Druid can't have haste anyway"* / *"only Arcum's Astrolabe is
relevant there"* / *"it's probably okay just to stick it first"*. The Druid is summoning-sick and the
Heart enters tapped, so neither funds the turn by going first; the fixer does. `SnowProvider::
CastOrderRank` now amends the generic order with Arcum's Astrolabe at 3 and the enter-watcher (Slumber,
`snow_enter_scry`) at 4 -- `MTG_SNOW_ORDER_WATCHER`, default ON (the measured-off Snow order carries the
same watcher slot). gi28 d8b0: 7 -> **6**.

## OPEN after 9380c934: two Snow games that did not recover at d8b0 -- root-caused 2026-10-04

9380c934 (the USER's Snow cast order, default ON) left two Snow games slower than GT at the d3 tier
that did NOT recover at `--depth 8 --budget-ms 0`: regression s3003 gi5 (GT 5, d8b0 6) and smoke
s1001 gi69 (GT 8, d8b0 loss). The isolation arms (`logs/m2q/iso.json`) put gi5 on the order (generic 5,
nofix 5, nosplit 5) and gi69 off it (generic also 9) -- except that dropping the fixer slot recovered
gi69 too. At d8b0 an order may only matter through rollouts past the horizon, condemnation, an
executor/rollout divergence or a dedupe interaction -- or, as it turned out, through PAYMENT.

### s3003 gi5: the cast order reached the search through the per-cast PAYER (fixed)

Diffed against the 27f10ea7 baseline (`/tmp/base-wt`), turn by turn: the first differing decision is
T2 main 1. Baseline: `Island; Astrolabe (draws Scrying Sheets), Frost Augur, Boreal Druid`, which the
T1 root's d5 pass verifies as a T5 win. Current: `Island; Astrolabe, Druid` -- no Augur.

The T2 plan `{Astrolabe, Augur, Druid}` IS enumerated in both binaries (it is jointly payable: Forest
{G}, Island {U}, the T1 Druid's snow {C} for the Astrolabe's {S}). But applied in the user order
(fixer 1, then cheapest-first with mana dorks ahead of other permanents: Astrolabe, Druid, Augur) it
realises as two casts. `MTG_FSW_TRACE` node child: `p=Island;Frost Augur,Arcum's Astrolabe,Boreal
Druid ... hand=[...,Frost Augur,...]` -- the Augur never left hand. A temporary per-cast print showed
why:

* `BatchPrepayMainCasts` -- the whole-turn joint payment that exists to stop exactly this -- DECLINED
  with `PP_PRODUCER`, because the Astrolabe carries `rock_mana` 1 (its "{1}, {T}: add any colour").
* The per-cast greedy then paid the Astrolabe's {S} with the FOREST (lands before the creature band),
  the Druid's {G} through Island + the fresh Astrolabe's filter, and the Augur's {U} had no source left.
* In the generic order the Augur came second and took the Island first, so the same greedy happened to
  survive -- the order only decided whether the greedy's stranding bit.

So it is (c)-adjacent: not a scoring or condemnation fault, an APPLY that realises a different line than
the one enumerated, in both worlds alike (the executor calls the same prepay), so no `[fd-diverge]`.
The jointly payable three-cast line was inexpressible at any budget in the user's order.

**Root cause:** the `PP_PRODUCER` decline treated a pure colour CONVERTER as a producer. Arcum's
Astrolabe's mana ability is 1-in/1-out (`filter_no_free_colorless`, the `ManaPool::wild_phantom` rule):
it adds no AMOUNT, so it cannot break the fungibility the decline protects. **Fix:** such a cast is
folded into the joint solve like any other (its conversion not credited -- a line that needs the
converted colour still reads combined-unpayable and declines to the per-cast payer exactly as before).
No flag: a bug fix. Only Arcum's Astrolabe carries the param, so every other deck is untouched by
construction. gi5 d8b0: 6 -> **5** (GT). The user's order is unchanged.

### smoke s1001 gi69: the SAME payer defect, on a different turn (fixed by the same change)

* **The GT 8 was never an unbudgeted result.** The 27f10ea7 baseline at d8b0 also LOSES gi69 (T8 ends
  with the opponent at 1). The d3/b10 GT win came from the T6 search's d1 pass, whose rollout leaf
  estimated T8 for `Island; Astrolabe, Augur, Coatl` before the budget ran out -- the deeper passes never
  ran. So the d3 regression was budget churn against a lucky GT; what needed explaining was why NO
  binary's unlimited search could find the T8 the d3 game realised.
* **Handoff at the T6 board** (`--claude-play --choices <T1-T5 of the game> --choices-then-auto
  --depth 8 --budget-ms 0`; seed 1070, `--choices "1,0,0,-1,0,0,0,0,0,0,0,0,0,0,0,2,0,0,0,0,3"`): baseline LOSES,
  97dec4b3 + 9380c934 + the prepay fix WINS T8 (d3/b0 from the same board: also T8). The T7/T8 kill
  lines are long Astrolabe chains (`Astrolabe -> draws Astrolabe -> Augur, Augur, Sheets look, Forest,
  Astrolabe -> draws Slumber -> Augur`), so every one of them carries an Astrolabe cast -- the
  `PP_PRODUCER` decline sent each to the per-cast greedy, which strands one of the {U} casts in some
  orders (the baseline's T8 fell exactly one Augur short). That is also why dropping the fixer slot
  "recovered" it in the isolation: Astrolabe LAST is an order in which the greedy happens not to strand.
  97dec4b3's continuation activations are part of the winning line (the Augur/Sheets looks inside a
  breakpoint continuation), which is why the baseline cannot reach it even from T6.
* Full game, d8b0, prepay fix: **8** (GT), 5,335 s. Isolation arm results recorded above were all on the
  pre-fix binary; the fix is the only change between `cur 9` and this `8`.

### Verdict

Neither game was a cast-order soundness problem (no condemnation, no rollout/executor divergence, no
dedupe): both were the whole-turn prepay declining on a colour CONVERTER and leaving the line to an
order-dependent per-cast payer. Fixed at the mechanism (`BatchPrepayMainCasts`, `conversion_only`); the
user's Snow order is unchanged and no amendment is proposed. Unit cover: `test/unit/test_snow_prepay.cpp`
(fails on the pre-fix code: the Augur is stranded in hand).

### Measured (prepay fix on 9380c934)

* d8b0 repro batch (`logs/m2q/rec2.json`, 12 jobs, one pooled batch): s3003 gi5 **5** at both d3 and d5
  keys (units 1,299,101 -> 93,748 -- the joint payment also collapses the duplicate per-cast-payment
  children), smoke gi69 **8**, every other prior repro unchanged (fungusb gi6/gi10/gi39 5, kittyv2 gi87 7,
  melira gi14/gi46 4, snow gi8 5, gi40 6).
* Smoke vs GT (game-turns, not accepted): snow searched -3 / d0 -8 (smoke snow d3 and d5 are
  digest-identical to the pre-fix run -- the change moves Snow d0 and the s3003 gi5 game), kitty -24,
  kittyv2 -24 / d0 -14, kitty2hg -5, goblins2hg -1, melira2hg -1; every other deck 0. Searched slower: 2
  -- melira gi46 4->5 (recovers at d8b0, recorded churn) and snow gi69 8->loss (d3/b10; d8b0 8 above).
  Only Arcum's Astrolabe carries `filter_no_free_colorless`, so no other deck's play can move.
