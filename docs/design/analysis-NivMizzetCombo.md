# Analysis ledger — NivMizzetCombo

Deck: `decks/NivMizzetCombo/NivMizzetCombo.cod` (U/R draw-trigger combo: 4 Niv-Mizzet, the Firemind,
2 Niv-Mizzet, Parun, 4 Curiosity, 4 Tandem Lookout; cantrips 4 Ponder, 4 Preordain, 2 Brainstorm,
2 Gitaxian Probe, 4 Remand; ramp 4 Izzet Signet, 1 Sol Ring, 4 Generator Servant; 21 lands incl.
3 Saprazzan Skerry, 2 Sandstone Needle, 4 Cascade Bluffs, 4 Spirebluff Canal, 1 Ferrous Lake,
1 Sulfur Falls, 6 Island). The user dropped the list into the primary checkout and asked for an
analysis on 2026-10-17.

Branch/worktree: `niv-analysis` @ `/workspaces/MagicDeckTester2/.claude/worktrees/niv` (cut from
origin `7d11c561e`). The decklist was COPIED here; the user's original stays in the primary checkout
untouched.

**The combo.** Either Niv + (Curiosity on that Niv, or Tandem Lookout paired with it) + any card draw
is a draw/ping loop: every draw deals 1 damage, every 1 damage to the opponent draws a card, until the
opponent is dead or the library is empty. Curiosity's draw is "may"; Lookout's is mandatory. Both
Nivs cost 6 (Firemind `{2}{U}{U}{R}{R}`, Parun `{U}{U}{U}{R}{R}{R}`), so mana colour fidelity is
load-bearing (Parun needs three of each colour; Generator Servant's `{C}{C}` pays only Firemind's
generic).

## Standing decisions (pass to every subagent)
- Goldfish: the opponent never casts spells or blocks; opponent-facing clauses are inert, but
  self-binding clauses bind us. Model the full card; goldfish-inert deferrals are PROVISIONAL until
  the user signs off.
- No greedy / heuristic substitute inside the search window (user hard rule). Choices a card
  introduces are SEARCHED, provider-narrowed (restrict/order only), or collapsed by a stated sound
  dominance fold — and surfaced to the viewer.
- Cards are generic (cards.json is deck-agnostic).
- Cast order / range / main-split / mulligan / discard buckets are USER-OWNED: author and present,
  never adopt unilaterally.
- One heavy job (build or batch) at a time; total threads <= 24. Other sessions share this box.
- Agents: no `cd` in compound commands, no `rm` / `rm -rf`, no `timeout`.

## Cast order -- the USER's (2026-10-31; `MTG_NIV_CAST_ORDER`, DEFAULT ON)

Before this the deck had NO cast order of its own: `NivMizzetComboProvider` overrode none of the order hooks,
so the generic tiers applied (Sol Ring and Izzet Signet TIED at 5, hand order deciding -- the Signet-first drop
in `mana-completeness-oracle.md` 13.7). The USER asked for one (*"we will need to make a cast order for Niv"*),
reviewed the draft (`build/scratch/castorder/drafts/NivMizzetCombo.md`) and approved it with one change --
Brainstorm FIRST among the draws, *"then you can pitch junk using the preordain and ponder"* -- and
*"Otherwise it looks sensible"*:

    land (searched, no pin) -> Sol Ring -> Izzet Signet -> Niv-Mizzet, the Firemind / Niv-Mizzet, Parun
    (name tie) -> Curiosity when its host is a Niv -> Tandem Lookout when a Niv is on the battlefield or
    earlier in the plan -> Brainstorm -> Ponder -> Preordain -> Gitaxian Probe -> Remand (with its target,
    ranked as the last draw) -> [no Niv: Curiosity -> Tandem Lookout] -> Generator Servant (cast)

The draft's defaults were accepted as the USER's rulings:
- **Q1 YES** -- the order binds inside cantrip (order-opaque) plans too: `OrderOpaqueCastsByRank`.
- **Q2 YES, amended** -- Q11 below is SIGNED OFF with the change that Curiosity / Tandem Lookout go ahead of
  the cantrips only when a Niv is in play or earlier in the plan (Curiosity: when its host is a Niv).
  `MayPrecedeCantrip` (TurnSolver.cpp) reads the same lever, so the enumeration and the apply sort agree.
- **Q3** -- the draws' sub-order is the USER's amended one (Brainstorm, Ponder, Preordain, Probe, Remand).
- **Q4 main 1** for both Nivs, Curiosity and Tandem Lookout (`MainPhaseOverride`). Advisory today: the deck opts
  into neither the main-phase filter nor condemnation, so this moves only `--cast-order-report` and the
  `MTG_ORDER_M1_FIRST` tie-break.
- **Q5 YES** -- the Generator Servant haste payment hole is fixed (`MTG_SERVANT_HASTE_KEEP`, 13.7 of the oracle
  doc; the USER: *"We should definitely fix the Generator Servant issue. I ran into problems with this in the
  viewer when I made a few references."*).

Implementation (by params, never names): `CastOrderRank` Sol Ring 4 (`mana_rock`, no `ramp_filter`), Signet 5,
Nivs 8 (`draw_trigger_damage`), Curiosity 9 / 22, Lookout 10 / 23, Brainstorm 12 (`put_back_from_hand`), Ponder
13 (`cast_reorder`), Preordain 14 (`cast_scry`), Probe 15 (plain `draw`), Remand 16, Servant 24
(`sac_for_mana_grants_haste`); `CastOrderActionRank` puts Curiosity at 9 only when its host (battlefield, or a
same-plan cast) is a Niv, and a Remand rider whose target is a draw at 16 (on a non-draw target it keeps the
target's slot, as the draft's section 7 specified); `ArrangeCastOrder` moves a Lookout to 10 when the plan casts
a Niv; `CastOrderTotalByName` breaks the Niv tie by name. `test/unit/test_niv_cast_order.cpp`.

Measured held out (seed 1640001, 2,000 Niv games per arm, on top of the two payment fixes): 31 slower / 34
faster, net -4, units 0.986x; 30 of the 31 slower games recover at `--depth 8 --budget-ms 0` (the same turn both
arms), and the 31st (gi1950, T4 -> T5) is T5 on both arms under the non-clairvoyant search (`MTG_NC_SEARCH=1`,
d8 b0): clear clairvoyance. A second seed (1650001, the whole package -- order + both payment fixes -- against
the old tree) is 43 slower / 46 faster (-2); its d8 b0 checks are recorded in `mana-completeness-oracle.md` 13.7: one game, gi887 (T4 -> T5), fails the bar
(T5 vs T6 under the non-clairvoyant search at d8 b0) -- the Preordain-before-Probe sub-order changes the T1 line
and the draw sequence; open, surfaced to the USER.
The order ships ON (the USER's order: -4 alone on seed 1640001; the package nets better on both seeds).

## STATUS 2026-10-24 ~11:45 UTC -- the keep table is ADOPTED; the value leaf is NOT
- **Keep table ADOPTED** (`decks/NivMizzetCombo/NivMizzetCombo.keepmodel.exhaustive.*`, fast R30, K=16, gen d1
  b1; 18.4 h, 5.09M rollouts on `cd4048e7e`). VALIDATION PASSED: keep -0.1528t (t -11.7, 4/4 seeds),
  confounded bottoming -0.0693t (t -8.5, 4/4). The overnight rows were first resized to Hinata's shape (4x400 b10
  / 4x300 b20 / 2hg 4x100) and accepted table-ABSENT (every pre-existing game identical). Adoption, overnight
  tier: searched d3 -0.050, d5 -0.040, 2hg +0.003 (flat), d0 about -0.15; every regression row faster. The overnight
  tier's Niv cost fell from 4m45s to 1m8s of makespan. keep_adoption_attribution explains every changed game but
  one per deck key, and that one is the no-table side's bottoming rollouts leaving memo entries behind
  (`docs/design/bottoming-rollout-cache-carry.md`, OPEN).
- **Value leaf: NOT adopted** (`valueleaf.sh run`, 2026-10-23 23:07 -> 10-24 10:12 UTC, on `cd4048e7e` with the
  keep table live). Phase E vs live play (8 seeds x 1000 paired games, d5 b20): staged leaf +0.0035t at 1.99x units
  (rejected: worse and dearer); leafless escalation (NLESC) -0.0115t (t -6.2, 8/0 seeds) at 1.54x; leafless fitted
  (NLFIT) -0.0080t (t -2.6) at 1.56x; emulated ladder (EMUL) -0.0108t (t -13.1, 8/0) at 1.29x. All three gains are
  TRADES, so the USER decides (the agent's recommendation: stay leafless this week; EMUL is the best trade if
  wanted). Artifacts: the niv-mullgen worktree's `logs/vlq_nivmizzetcombo/` (DECISIONS.txt has the copy commands).
  Phase C matrix: H5 4.3913 at 26.3 s/game vs V7 4.3913 at 24.0 s; trust depth UNSET (V5 gap +0.0057
  inconclusive).
- **Why every leafless shape measured dearer than the rollout ladder, PARKED (USER 2026-10-24):**
  `docs/design/niv-rollout-ladder-vs-leafless-shapes.md`. USER: the rollout ladder should be replaceable by a
  leafless shape up to the final rollout. Measured on Niv: earlier rollout passes are 13.9% of the ladder's
  units; the built variants (emulated ladder, NLESC, NLFIT, reserved single pass) all cost 1.01-1.59x. That doc
  gives the root causes and the proposal (A: a frozen-R leafless-warm-up shape that cannot lose to the ladder;
  B: cut the final pass, the put-back dedup).

## STATUS 2026-10-23 ~03:45 UTC -- supersedes the 10-18 hand-off below where they differ
- **In the regression suite** since 2026-10-22 (`9cb575dbd`, all three tiers, GT `01172bba8`); buckets K19 -> 16
  (USER: "somewhat reasonable"). The unbudgeted backstop block is still owed (item 5 below).
- **The USER-adopted cantrip package is DEFAULT ON** (`052a885a2`, landed with GT `cd4048e7e`): MTG_NIV_CANTRIP_RULES
  + MTG_NIV_CANTRIP_AMEND, MTG_NIV_PONDER_KEEP_SHUFFLE, MTG_NIV_BUCKET_DISCARD, MTG_NIV_BRAINSTORM_RULE +
  MTG_NIV_BRAINSTORM_SINGLE. Held-out confirmation (seeds 71000 d3 b10 x600 / 72000 play x300, vs the old play):
  d3 -0.0755 (t -3.92, 73 faster / 35 slower), play -0.0408 (28 / 17), 0.24x / 0.33x work units; the amendment
  alone is worth -0.0183 d3 / -0.0100 play, so it stays ON. Still OFF: PONDER_SINGLE, LOOKOUT_RULE2 (+ _SERVANT),
  SERVANT_UNPAIR, LOOKOUT_NIV_ONLY, the MTG_NIV_ORDER_* rules (the latter two families and the MIDDLE / BAND
  put-back prunes are on branch `niv-brainstorm`, unlanded, pending their A/B).
- **The mulligan-generation memory blow-up is fixed** (`88a979f07`..`a0fe3421b`, `docs/design/niv-putback-fan-memory.md`):
  the pool retention cap + MTG_PB_LAZY_CANDS (DEFAULT ON, byte-identical, VERIFY 0 mismatches). Discovery at
  d5 b20 peaks at 15 GB (it died at ~40 GB).
- **Stage 5c2 DONE 2026-10-23** (item 4 below): `leaf_tiebreak_check.py` at the deck's play cell, 12 blocks x
  1000 paired games on `cd4048e7e`: net **-20 turns** (30 better / 13 worse, binding 0.358%; half A -2, half B
  -18) -> **VERDICT: KEEP THE DEFAULT (ON)**. Despite the stored-value combo shape the check warns about, the
  horizon tie-break lowers Niv's average.
- **Mulligan profile generation RUNNING** since 2026-10-23 03:42 UTC (`mullgen.sh run decks/NivMizzetCombo fast`,
  worktree `niv-mullgen` at `cd4048e7e`).

## HAND-OFF (2026-10-18 ~22:00 UTC) -- read this first
Everything is PUSHED to `origin/phase-1-2-deck-analyzer` (USER 2026-10-18: "Let's get everything pushed and we'll
sort out the remaining issues during the week"). The session's worktrees are retired; this file and
`docs/design/niv-cantrip-branching.md` are the whole state. No run is in flight.

**Where the deck stands.** Cards implemented and reviewed (19/19 full), provider `NivMizzetComboProvider`,
Stage-4 profile committed, verify_deck green except the expected gates below. NOT in the regression suite:
the 3x cost rule fails, **11,604 ms/game** (worst `niv_regression_d5_s3003`) vs the 8,086 limit (Stage 5j).
The value leaf and the mulligan profile are BLOCKED on suite membership (`suite_gate.py --require`).

**To re-admit it** once `python3 scripts/suite_gate.py --cost decks/NivMizzetCombo` would pass:
`git apply docs/design/analysis-NivMizzetCombo.suite-rows.patch` (all three tiers at Landfall's /
Galadriel's pre-keep-table shape; applies cleanly to `test/regression_cases.sh` as of this push), run
`suite_gate.py --measure decks/NivMizzetCombo` (NOT `--cost`, which re-measures), then the tiers + accept.

**Open for the week (all the USER's unless marked):**
1. **Q15 -- Niv's bottom-eval setting**: the one lever that clears the 3x gate now (mulligan bottoming
   with no keep table is the dominant cost). Measurements in Q15.
2. **The cantrip heuristics** the user flagged (Ponder / Preordain / Brainstorm):
   `docs/design/niv-cantrip-branching.md`. Q14 (Preordain's scry is a provider prune via
   `GenericProvider::ScryKeepOnTop`, which bottoms every land once two are in play -- wrong for a
   6-drop deck), Q16 (the global 25x overrun ceiling).
3. Q12 (the two payment levers ship ON), Q13 (s71014 T1 land order), Q10 (Hinata d0 Remand riders),
   Q1-Q9, Q11, and the claude-play sweep's 3 deferred low/cosmetic items.
4. **UN-RUN: Stage 5c2** `python3 scripts/leaf_tiebreak_check.py decks/NivMizzetCombo/NivMizzetCombo.cod`
   (mandatory for a new deck, and Niv is the stored-value combo shape the check warns about). Not run:
   it takes all 24 cores, and the session closed first. Run it when the deck is next worked on.
5. **Agent-side: an unbudgeted Niv game explodes.** `./build/Release/mtg decks/NivMizzetCombo/NivMizzetCombo.cod
   --profile decks/NivMizzetCombo/NivMizzetCombo.profile.json --games 1 --seed 1 --depth 1 --budget-ms 0
   --ignore-play-profile` ran 7 h without finishing (it stalled `provider_audit --check`, which now
   budgets its routing game). perf: `AppendSubdecisionAxes` / `BpDeriveContinuationList` /
   `BpEnumEntryFor` / `AppendSoulbondDeclineVariants` -- breakpoint continuation lists with the
   put-back / Ponder / soulbond axes, the same family the 10-18 perf pass cut for budgeted play.
   Unbudgeted completeness is the invariant, so this is a cost to find, not a setting.
6. Agent-side: `scripts/play_invariants.py` still lacks 14 emitted decision types (attack_mode, echo,
   jitte, revive, ...); each needs its reply shape checked when a deck reaches it.
7. The OVERNIGHT GT is stale for the decks this work moved (hinata, goblins, bruna, ...) -- left, by
   design, for the periodic overnight run.

Commit map (oldest -> newest): cards + integration (Stage 1-2), the breakpoint un-gate `c52ed5c9d`-era,
Brainstorm cost + nested bail, Aura legality + cantrip-first VALUE, LIFO draw loop, payment P1/P2,
search-cost cut (canon front-only), bottoming turn cut, GT accept, profile, tooling fixes.

## Stage 1 — coverage (2026-10-17)
Missing (8): Niv-Mizzet, the Firemind; Niv-Mizzet, Parun; Curiosity; Tandem Lookout; Generator
Servant; Brainstorm; Gitaxian Probe; Sulfur Falls.
Present (11): Remand, Island, Cascade Bluffs, Ponder, Izzet Signet, Sol Ring, Saprazzan Skerry,
Sandstone Needle, Preordain, Spirebluff Canal, Ferrous Lake. No sideboard.

Bracket notes reclassified for this deck:
- **Remand** (`goldfish_inert`): the note says "no legal target, never cast". Wrong in general —
  Remand can target your OWN spell (CR 115.1: any spell on the stack). In this deck a self-Remand is
  a 2-mana draw (one Niv ping), a cast trigger for Parun, and against an uncounterable Parun it is a
  pure cantrip (the counter fails, "Draw a card" still happens). Tier 3 gap, NOT an accepted
  deferral. Remand is also in Hinata2 and Unpredictable Cyclone, so the fix is a play change there.
- **Izzet Signet / Ferrous Lake** (`ramp_filter`, U+R "treated as wild"): the note was written for
  Hinata ("the deck's dedicated W sources cover Hinata's {W}"). Parun's `{U}{U}{U}{R}{R}{R}` makes
  the colour split load-bearing here — fidelity to be checked (Stage 2).
- **Ponder**: all-or-nothing reorder/shuffle with the `ponder_keep` plan variant; previously
  accepted, unchanged.
- **Spirebluff Canal**: fully modelled.

## Stage 2 — cards (in progress)

Engine survey (2026-10-17, three read-only agents):
- **No per-draw trigger exists**; draws happen at ~26 sites (`cards_drawn_this_turn +=`), several
  helpers. Non-combat damage carries no source. No Aura grants a triggered ability. Soulbond exists
  (Silverblade Paladin) with a searched pair-or-wait and a viewer prompt. Creature `{T}: draw` exists
  (`tap_draw_cost`). Every triggered "any target" damage in the engine goes to the opponent's face
  (Pashalik Mons / Omnath precedent).
- **No stack window**: every cast resolves before the next; nothing can target a spell. Self-Remand
  is modellable as one combined action (cast X + Remand X: both costs, both cast-time blocks, X
  returns to hand, Remand draws), the splice / same-plan trick-target shape.
- **Mana**: the shared payer is exact for Signet / Ferrous Lake (U+R). The FLAT affordability pool
  over-credits single-colour feeders (Island+Island+Signet "affords" three `{U}` spells; the payer
  then refuses) and under-credits Sol Ring feeding a Signet (C+U+R). Phyrexian mana and checklands
  already exist. Sac-for-mana exists but has no summoning-sickness gate and no spend rider.

Work split (2026-10-17 ~22:40 UTC), one worktree + branch each, integrated serially here:
- `niv-core`: shared draw-trigger hook + damage-to-opponent watcher; Niv-Mizzet, the Firemind;
  Niv-Mizzet, Parun; Curiosity; Tandem Lookout; same-turn reachability of trigger-drawn cards.
- `niv-remand`: self-targeted Remand (combined action, lossless target folds, viewer), incl. the
  can't-be-countered Parun cantrip case; revisit Hinata/Fluctuator "never cast" hooks.
- `niv-mana`: Generator Servant (tap-gated sac mana + haste-rider pool), Gitaxian Probe, Sulfur
  Falls, Brainstorm (searched put-back, viewer `put_back`), Signet/Sol Ring feed fidelity.

### Card table
| Card | Tier | Summary | Deferrals (status) |
|---|---|---|---|
| Niv-Mizzet, the Firemind | 3 | `draw_trigger_damage 1` via the new shared iterative loop `ResolveDrawTriggers` (watermark on `cards_drawn_this_turn`, flushed after every drawing resolution in both worlds, stops at the kill); `tap_draw_cost "{0}"` (shared TapDraw, sickness via CanTapNow). | "any target" → opponent's face, human not asked — PROVISIONAL (Q2) |
| Niv-Mizzet, Parun | 3 | same draw trigger + `cast_trigger_instant_sorcery_draw 1` in `FireOnCastTriggers` (copies don't count); `cant_be_countered` (read by Remand). | as above (Q2) |
| Curiosity | 3 | `is_aura` (host searched; Aura host ranking gains a `combo` term so Curiosity goes on a pinging host — Q5) + `aura_damage_trigger_draw` from the new `OnCreatureDealtDamageToOpponent` (Niv ping + combat damage). | "may draw" → draw iff library non-empty — PROVISIONAL (Q3). Non-combat damage by a NON-Niv host not modelled (no such host in any deck) — PROVISIONAL |
| Tandem Lookout | 3 | soulbond (Silverblade machinery) + `soulbond_damage_trigger_draw` (both halves, mandatory, stops at the kill; empty-library draw loses). `SoulbondPartner` ranks a Niv first; the decline ("wait") twin also when no Niv can take the pairing (Q6). | — |
| Generator Servant | 3 | `sac_for_mana_amount 2` / `C` + `sac_for_mana_requires_tap` (sickness binds) + `sac_for_mana_grants_haste` (tagged floating {C}, a creature spell spends one first → `temp_haste`). | payer spends floating before lands, so a noncreature cast after the crack can eat the tagged {C} (the search can order the crack later) — disclosed |
| Brainstorm | 3 | `draw 3` + `put_back_from_hand 2`, put-back inside the resolution (draw triggers wait, CR 603.3); WHICH two + order SEARCHED (`Plan::putback_choice`, every distinct ordered pick, identical copies folded, provider-ordered by the cleanup shed order, NOT capped). Median fan ~76/plan. | instant windows → main phase — PROVISIONAL; a 2nd Brainstorm in ONE plan takes the default (Ponder-axis precedent; a breakpoint re-solve gives it its own fan) — disclosed |
| Gitaxian Probe | 2 | `draw_spell draw 1`; Phyrexian twin (Birthing Pod machinery), never to 0 life. | "look at target player's hand" not modelled (information only) — PROVISIONAL |
| Sulfur Falls | 1 | `checkland_subtypes [Island, Mountain]`. | — |
| Remand (reclassified) | 3 | self-targeted: rides X's cast as `Action::remand_by`, both costs, both cast-time blocks, X back to hand (resolves if `cant_be_countered`), Remand draws (flushes draw triggers before X resolves); every target its own variant, identity folds only. | instant windows → main — PROVISIONAL; Remand-on-Remand, free/alt-cost targets, cascade targets not modelled (in no Remand deck) — PROVISIONAL |
| Izzet Signet / Ferrous Lake (fidelity) | 2 | payer exact; colour test charges the feed (`MTG_RAMP_FEED_CHARGE`), branched feed pick on failure (`MTG_RAMP_FEED_BRANCH`), sac floats feed the filter rescue (`MTG_RESCUE_SAC_FLOAT`); all default ON, `=0` hatch, unit test fails with each hatch off. | — |

Integration (2026-10-18 ~00:55 UTC), `niv-analysis`:
- `61394bfa2..1ca848678` niv-core; `0af351c29`, `2165ed830` niv-remand; `43b4582d6` integration (Remand's
  draw flushes the Niv triggers before an uncounterable X resolves -- Parun pinged for a draw made
  before it entered; the unit test fails without the flush, verified); `067b3a1db` the deck's provider
  (Stage 4a, below); `15b0f9069..4f800e59d` niv-mana; `3b9c54d5e` two cross-branch fixtures (Servant
  haste → Firemind {T} loop; a self-Remand as the loop's draw), both negative-controlled.
- `c52ed5c9d` UN-GATES niv-core's three breakpoint fixes (SectionHandGainedACard's empty snapshot, the
  plain-TapDraw PlanOpensBreakpoint route, the executor's fallback re-solve). The agent had gated them
  on `deck_has_draw_ping` to keep other decks byte-identical; the holes are general (Troyan, Mariposa,
  Frost Augur / Scrying Sheets, any fallback main), so they are measured as a play change.
- Tests on the integrated tree: unit 1482/1482, scenarios 229/229.
- Stage 3: coverage 19/19 `full`, missing none. Cost audit on the deck's 12 costed cards: rc 0, 12
  compared, all match. Field audit: every hard field matches; rc 1 only from three Temples already
  absent from the snapshot at HEAD.

### Isolation tiers on the integrated + un-gated tree (T2 = `c52ed5c9d`, 2026-10-18 ~01:10-01:36)
Arms for attribution, each run from its OWN worktree (cards.json is read at runtime):
base = `niv-base` (origin src), T1 = `niv-mana` (integrated, still gated), T2 = `niv`.
`logs/arms/arm_compare.py` / `arm_lines.py` (d5 cases reproduced WITHOUT --depth, as the suite runs
them: value_play owns the depth).

- unit 1482/1482, scenarios 229/229; viewer reference replay 600 refs: 196 ok, 404 repaired,
  0 play-drift / shuffle-dead / board-diverged / enum-gap / mull-drift / contract-fail.
- **smoke** rc 1: hinata d0 6.723->6.783, d3 5.640->5.653, d5 5.827->5.787, hinata2hg d3
  5.573->5.587; same-avg digest moves in auras d3/d5/2hg, goblins d0, kittyv3 2hg, bruna d0/2hg,
  snow d3. Searched slower 4 / faster 6.
- **regression** rc 1: hinata d0 6.791->6.849, d3 s2002 5.570->5.560, d3 s3003 5.645->5.615, d5
  s2002 5.690->5.660, d5 s3003 =, 2hg d3 5.500->5.460, 2hg d5 5.520->5.540; breaching d0
  4.832->4.826; same-avg digest moves in goblins d3/d5 (all four) + 2hg, bruna d0/d3, fungusv2 d0.
  Searched slower 15 / faster 28.
- **Attribution:**
  - Every searched SLOWER game (4 smoke + 15 regression) is HINATA, from T1 (the Remand rider),
    not the un-gate (T1 == T2 on all of them). All 19 reproduce at the case budget; at UNLIMITED
    budget (d3 pinned, d5 via value_play) new <= base on EVERY one: smoke gi11/88/135 7/7/5 both;
    regression 13 tie, d5 s3003 gi64 new T5 vs base T7, d5 s3003 gi27 loss in BOTH (base's b20 T8
    was itself budget luck) -> budget churn from the wider plan set, no line lost
    (`logs/arms/hinata_reg_b0.txt`, `hinata_smoke_b0.txt`). Example gi135: the line
    diverges on T1 (Preordain vs Gamble) in a game that never casts Remand -- dilution.
  - goblins / bruna / snow / auras / kittyv3 moves are the UN-GATE (T1 != T2): a card put into
    hand mid-turn is now cast that phase (goblins gi213: + Muxus on T3; snow gi68: Boreal Druid a
    turn earlier; auras gi8: the drawn land is played; kittyv3 2hg gi16: four more pieces on T7).
  - breaching d0 gi21 (loss -> T4: Sakashima's Protege paid off Ferrous Lake) and fungusv2 d0 are
    the MANA-FIDELITY fixes (both revert to base with the three hatches off).
  - Hinata d0 got WORSE (+0.058): the d0 runner (a permitted greedy) is offered Remand riders and
    e.g. gi15 T5 picks "Soulfire + Remand" over an available lethal line. Investigation agent on
    `niv-d0` (is it a valuation defect that also reaches the search?).

### Brainstorm cost + silent Aura retargets (2026-10-18, branch `niv-aura`)
Stacked on niv-analysis in this order: the Servant host fold (`669124c36`), the niv-bs agent's three
commits (cherry-picked), the Aura/cantrip bundle (`41950c0f0`).
- **niv-bs (Brainstorm cost agent).** `862224397` closed-form put-back pick (lossless: 400 random hands
  rank-for-rank; 120 discovery games identical; core-ms 2.26M -> 1.04M d5, 1.56M -> 0.75M d3).
  `11f767ef7` + `718aa7115`: `MTG_LA_NESTED_BAIL` (a nested solve honours a tripped overrun ceiling;
  never read at unlimited budget) ADOPTED ON -- 570/570 games byte-identical, units 0.93-0.96x, worst
  game 11.79M -> 1.66M units; `MTG_PUTBACK_PRICE` built, left OFF (9 slower / 1 faster d3, +0.027;
  "+1 is not neutral"). Its A/B ran with NO profile on disk; the bail is general (any deck whose
  lookahead ceiling trips) -> the suite tiers are its test. The agent ran one `rm -f` on its own
  scratch (against the no-rm rule; disclosed).
- **The retarget meter** read 93,663 on 20 games (d3 b10 s5001) -- the search pricing an Aura onto a
  host the plan itself removed, then casting it onto a fallback. Census by the host's zone at
  resolution: sacrificed for mana by the same plan (Generator Servant's pre-pass) -> subset fold
  `SubsetHasHostSacrificedForMana` (-84%); of the 14,769 left, LIBRARY 8,266 (the plan's Brainstorm put
  the host back), HAND 5,823 (the plan self-Remanded it), GRAVEYARD 680. Fixes (`41950c0f0`):
  `AuraSpellCastLegal` (CR 601.2c: no legal target at cast time -> not cast, both worlds, before
  paying) and Brainstorm returns the LAST copy of a class (casts take the first). Meter: 0.
- **Lost kill found on the way (play change, Q11).** Cantrip-first (searched play only) hoisted
  cantrips over the Niv payoffs, so "Curiosity onto Niv, then Preordain" was in no d3/d5 plan list.
  `MayPrecedeCantrip`'s documented VALUE category now holds the four loop params; a hoisted Aura
  carries its same-plan host; `AppendCreatureTargetAuraCandidates` now sets `Action::def`; the
  EnumeratePlans call runs after the Aura end-sorts (identical order for every other action).
- **A/B** niv-bs vs niv-aura, profile loaded, held-out seeds: d3 b10 x150 (s61000) avg 4.733 ->
  **4.680**, 1 slower / 7 faster, retargets 752,104 -> 0; d5 b20 x60 (s62000) 0 slower / 1 faster
  (one base game still running at the time of writing). Slower gi73 d3 at --budget-ms 0: pending.
  **Disclosure:** the first gi73 b0 pair (08:56-14:15 UTC, ~5.3 h each, both arms) ran
  `--seed 61000 --game-index 73`, which is a DIFFERENT game -- the A/B's gi73 is `--seed 61073
  --game-index 73` (base + gi). Stopped unfinished; the corrected command reproduces the A/B at b10
  (base T4, tip T5) and was relaunched at b0 on niv-bs and the current tip.
  **Result:** the tip (`25fd4d075` src) at d3 `--budget-ms 0` wins gi73 on **T4** (= base's b10 T4; 28.6 min,
  117M units) -> the A/B's one slower game RECOVERS at unlimited budget. Base's b0 run stopped (not needed).
- Unit 1490/1490, scenarios 229/229.
- **ab2 (the LIFO draw loop, `0d0892e19`, on top of the bundle)**, same cells, base arm reused from ab1
  (same niv-bs binary): per-game outcomes IDENTICAL to ab1's niv-aura arm in both cells (d3 4.680,
  d5 4.633; only the completion order of the `[win]` lines differs). The loop fix only changes
  near-empty-library kills, which these 210 games never reach.

### Payment holes P1/P2 fixed (2026-10-18 ~11:10, the niv-pay agent; `d9e002b8e`, `de36ef8ae`)
- **P1 `MTG_PREPAY_FLOAT_POOL`** (default ON). `PP_FLOAT_NZ` existed only because the pre-load
  overwrote `floating_mana` (`f154d64b1`); both apply worlds crack SacForMana before the prepay, so every
  Servant line fell to the per-cast greedy. The joint solve now STARTS from a plain float (WUBRG/{C}; a
  wild or snow float still declines) and the pre-load is built from its output; the Servant's
  haste-tagged {C} stays {C}. The two split-bill fallbacks are skipped with a counted float.
- **P2 `MTG_RAMP_FEED_ROUTE`** (default ON). In the shared per-cast payer (`TapForCostShared`): when a
  cheap precheck passes (casts still owed, an untapped ramp filter, the payment could tap every
  feeder), the greedy is trialled on a scratch board; if it strands the filter and the rest of the line
  becomes unpayable, a second trial routes the last feeder through the filter and is taken only if the
  rest of the line then pays (one backtracker solve). Mana payment is the standing exemption; executor
  and rollout share the payer (lockstep). Also live in human play (the viewer's plan menus lose these
  drops).
- Tests: 4 new unit cases (`test_niv_payment_holes.cpp`; each lever =0 reproduces the drop), 1495/1495,
  scenarios 229/229, the 600-reference `viewer_protocol_check --strict` identical old vs new.
- **Held-out A/B** (profile loaded; control = niv-aura `f05550dc7` src): d3 b10 x100 (s75000) 4.740 ->
  **4.660** (9 faster / 1 slower); d5 b20 x40 (s76000) 4.625 -> **4.600** (2 / 1). Both slower games
  recover at `--budget-ms 0` (d5 gi21 s76021: T4 = control; d3 gi77 s75077: T5 = control). CPU +4.7%
  (ramp-route trials ran 4.2M times over 140 games, fired 0.5M).
- Acceptance seeds (d5 b20): **s71004 T5 -> T4** (Claude's exact line); s71011 T4 only at b2000 in both
  binaries (budget, not a hole); s71014 stays T5 at b20/b200/b2000 -- its T4 plan is offered undropped in
  both binaries once Claude's T1-T3 are replayed; the AI loses it at T1 (Spirebluff Canal instead of
  Saprazzan Skerry), a SEARCH question (Q13).
- Other decks that can move (attribute in the tiers): float lever -- Dragonstorm (Lotus Bloom),
  BreachingDragonstorm (Dwarven Ruins, Svyelunite Temple), Mirrorwing's mint-prefix prepay, any re-solve
  starting with leftover float; ramp route -- treasure_hunt, Hinata2, BreachingDragonstorm, Bruna. The
  agent's quick probe (d0 x300, d3 x40 per deck, fresh seeds): breaching/dragonstorm identical, th/bruna
  digests moved with equal averages, hinata d0 1 faster, no slower games.

### Search cost cut (2026-10-18 ~13:30, the niv-perf agent; `dccf2ec51`..`80aeeca6c`)
- **Root cause of the per-unit explosions.** `BpUnbranchedCanon` reads ONE entry (`front()`) of a
  breakpoint continuation list but derived and cached the whole list, Brainstorm put-back clones
  included: on s2018 gi16 d3 one T6 decision took 151 s for 225k units (the 25x overrun ceiling), 88% of
  it in those builds -- 46M clones from 101k derivations (one list: 18,015 clones of 555 plans), 32 cache
  clears, 2.8 GB RSS. Interior work the budget never charged.
- Fixes (all SOUND, default ON, hatches): `MTG_BP_CANON_FRONT` (front-only list without the put-back or
  Ponder clones; `MTG_BP_CANON_FRONT_VERIFY=1` checked 167k builds against the full list, 0 mismatches),
  `MTG_BP_NODE_PUTBACK_FOLD` (a breakpoint child whose put-back rank clamps onto one a sibling applied
  is skipped; still charged its unit, so units do not move), and the front-only route only when the
  hand holds a Brainstorm or a Ponder (every other deck keeps its caching and units exactly).
  Diagnostic `MTG_CANTRIP_ATTRIB` (default off).
- Identity (suite-shaped 160 games): 160/160 same win turn + play digest; 158/160 same units (2 differ
  by -1 / -36, breakpoint-cache residency; identical with `MTG_NO_BP_ENUM_CACHE=1`). Hinata2 90/90 same
  play, 89/90 same units.
- Per game: s2018 gi16 d3 99.4 -> 8.6 s; s3007 gi4 d3 980.9 -> 38.4 s; s3007 gi4 d5 1201.2 -> 53.2 s;
  s2029 gi27 d3 180.8 -> 149.4 s (mulligan-bound). Cases (ms/game): d3 s3003 29,856 -> 7,161; d5 s3003
  69,218 -> **14,071** (the worst case; quieter runs 10.6-11.7 s); 2HG d3 25,571 -> 7,489. Still OVER
  the 8,086 ms limit on the agent's shared-box reading -> the official `suite_gate.py --measure` re-run.
- What dominates now: **mulligan bottoming** (no keep table: one full game per legal bottom subset):
  51-56% of d3 wall, ~64% of d5 / 2HG; the three heaviest d5 s3003 games are 95-98% of it (Q15). 45-99%
  of units sit in passes aborted at the 25x ceiling (Q16). Payment trials <0.6% self time.
- Cantrip census + options for next week: `docs/design/niv-cantrip-branching.md`.
- Disclosures (the agent's): one `cd` in a compound command; two `pkill -f` self-matches; one edit of
  TurnSolver.cpp mid-compile (the stale probe binary was discarded and rebuilt). It left
  `.claude/worktrees/niv-perf-base` (detached at `de36ef8ae`) in place.

### Tiers T3 (pre-rebase `25fd4d075`) -- what moved since T2, and who owns it (2026-10-18 14:09-14:35)
Niv's suite rows set aside (`logs/niv/niv_suite_rows.patch`, deck not admitted: 3x rule). Compared case by
case with T2 (`logs/niv/t2_t3_cmp.py`), then per game.
- **smoke:** 121 identical, 7 moved, ALL d0: bruna / dragons / fivecolour / mirrorwing / selesnya ->
  `MTG_PREPAY_FLOAT_POOL` (=0 restores T2's digest), th -> `MTG_RAMP_FEED_ROUTE`, hinata d0 -> neither
  (both =0 still differ; the only other post-T2 commit reaching Hinata d0 is the Remand-X credit fix
  `a21854b5f`, whose own report was "Hinata d0 2 faster / 1 slower"). Every SEARCHED smoke case is
  byte-identical to T2; vs GT searched slower 4 / faster 6 = T2's (all Hinata, the Remand rider).
- **regression:** 164 identical, 13 moved. d0: bruna, dragons, fivecolour, fungusv2, hinata, mirrorwing,
  selesnya, th (same owners). Searched: hinata d3 s2002 / d3 s3003 / d5 s2002 / d5 s3003, fivecolour2hg d3
  -- per game, every move is the SAME win turn except hinata d3 s3003 gi128 **5 -> 4**, owned by
  `MTG_LA_NESTED_BAIL` (=0 alone restores T5; a re-price, inert at unlimited budget). No searched game
  got slower T2 -> T3. vs GT: searched slower 15 / faster 29 (T2: 15 / 28).
- **The d0 slower games** are the permitted greedy, e.g. mirrorwing d0 gi236 5 -> 6 (`MTG_PREPAY_FLOAT_POOL`):
  the float now lets T4 pay Ancestral Anger alongside Zada (the old payer could not), and the greedy
  runner's different T5 wins a turn later -- churn, not a payment defect.
- Then REBASED onto origin `2176032dd` (45 commits: Galadriel scry rules + value leaf + keep table, Colossus
  package, GT re-accepts). One conflict (`tools/play/DECISIONS.md`, both rows kept); range-diff: every
  other commit patch-identical. T4 = the same tiers on the rebased binary, against origin's GT.

### Tiers T4 (rebased, `b01c51cd3` src `c34f2d3d2`) and the GT accept (2026-10-18 14:35-15:00)
- unit 1511/1511 (origin added cases), scenarios 229/229. smoke + regression: every case byte-identical
  to T3 except galadriel / colossus (origin's own changes), which match origin's GT exactly -- this
  branch moves neither. Refs (viewer protocol --strict): 630, 0 drift / gap / contract-fail.
- vs GT: smoke searched 4 slower / 6 faster, regression 15 slower / 29 faster; the searched-SLOWER sets
  are IDENTICAL to T2's (all Hinata, the Remand rider, recovered at unlimited budget at T2).
- ACCEPTED smoke + regression (`4c13e2ac6`) with one-line provenance notes naming every owner; the
  dropped colossus notes' USER rulings are carried in them (their cells did not move).
  `check_gt_logs` 735 consistent. The OVERNIGHT GT is left for the periodic overnight run.

### Stage 4 -- analyzer (finished 13:31 UTC on the `c52ed5c9d` binary; profile committed `bff87208d`)
- mulligan_flags: none. Mulligan profile defaults (min_lands 1, max_lands 5, no hand-score gate).
- Cost diagnostic: COST_NEUTRAL (avg 4.675 both arms, 200 games d5 b20).
- **Discard analysis: STATUS_QUO_OK** -- 308 cleanup decisions over 400 games (264 at hand 9+, 44 at
  hand 8), EVERY one multi-optimal, status-quo regret 0, derived order empty -> no discard buckets to
  author. (Measured before the Aura / payment / perf fixes; with every decision tied, they cannot
  plausibly change it.)

### Stage 5 -- verify_deck full run (2026-10-18 15:05-15:20, rebased tip `5354901ca`)
PASS: coverage (19/19 full), card_costs (Scryfall), viewer (self-guard + surface sweep), viewer_wiring
(attach_host, dig, reorder, scry), **mismatch: no [nonconv] / [fd-diverge] across seeds 7001, 8001 x 60
games**. SKIP: card_fields (the snapshot lacks 3 Temples from another deck), clause_ledger (covered).
FAIL, all expected / the user's: regression_tiers + suite (the 3x rule -- sign-off keys
`regression_tiers:too_slow`, `suite:not_a_case`), claude_sweep (the 3 deferred items).
**play_invariants** first FAILED on a HARNESS gap: the driver did not know the `attach_host` decision
type (Tandem Lookout's soulbond pairing; viewer_wiring was already green). Added to its KNOWN_TYPES (a
one-int decision whose engine pick is `heuristic_default`, which the generic reply follows) -> 8 games,
200 decisions, 0 hard, 0 advisories. 14 other emitted types (attack_mode, echo, jitte, revive, ...) are
still unknown to the driver; each needs its reply shape checked when a deck reaches it.
5b multi-depth (from the suite cells and the held-out A/B): d0 6.573 > d3 4.675-4.725 >= d5 4.633-4.70 --
monotone and on the deck's T4-T5 clock.

### Stage 5j — the 3x cost rule: FAIL (2026-10-18 ~09:45)
`suite_gate.py --measure decks/NivMizzetCombo` on the niv-aura tree (`d09f881e7` src), the rows of
`logs/add_niv_rows.py` in place (uncommitted): **124,303.95 ms/game** at the worst searched case
(`niv_regression_d5_s3003`), 8,459.4 core-s over the six regression jobs. Budget = 3 x pd 2,695.2 =
8,085.6 ms/game -> **15.4x over the limit (46x pd)**. Measured on a box ~2x oversubscribed (load 35-52
on 24 cores: another session + the perf agent's builds), which inflates core-ms; halving it is still
~8x over. The cost is dominated by outlier games whose per-UNIT cost explodes (d3 b10 s3007 gi4: 1,507 s
for 1.64M units; s2018 gi16: 347 s for 246k units -- 15-25x the normal per-unit cost: work the budget
never sees). PER THE RULE: the deck is in NO tier (rows not committed), and getting it under 3x is
the first goal, ahead of the value leaf and the mulligan profile -- perf agent on `niv-perf`.
(A re-run via `--cost` was started by mistake and stopped within a minute -- it re-measures; it
overwrote `test/logs/regression/batch.log`'s per-case breakdown. The cached figure above stands.)
- **Re-measure after the search-cost cut** (2026-10-18 13:37-13:47 UTC, `suite_gate.py --measure`,
  niv-aura `80aeeca6c` src, THREADS=20 on a quiet box, load ~3): **12,096.7 ms/game** (worst
  `niv_regression_d5_s3003`), 1,372.6 core-s (was 8,459.4) -- 10.3x cheaper, still **1.50x over** the
  8,085.6 limit (4.49x pd). Per case (ms total / games): d5 s3003 241,934/20, d5 s2002 122,449/20, d3
  s3003 332,412/40, d3 s2002 320,923/40, 2HG d3 354,607/40, d0 289/1000. The worst case's gi4 (67 s),
  gi8 (59 s) and gi3 (34 s) are 66% of it -- all bottoming-probe games. Logs kept in
  `build/scratch/niv-aura/measure2/`.
- **With the bottoming turn cut** (`MTG_BOTTOM_TURN_CUT`, `25fd4d075`, SOUND: all six Niv digests and
  .wins byte-identical): **11,604.25 ms/game** (worst d5 s3003), units there 9.29M -> 7.86M. Still
  **1.44x over**. What remains is the bottoming probes' turns BEFORE the best win turn (most candidate
  bottoms tie); the one lever left that clears the gate is the bottom-eval setting (Q15, the user's).

### Stage 4a — provider
`NivMizzetComboProvider` (`067b3a1db`): an empty `DeckProvider` derivation (the deck rode Generic),
routed at the top of `SelectDecisionProvider` by four params OR'd across four cards no other deck
carries (`draw_trigger_damage`, `cast_trigger_instant_sorcery_draw`, `aura_damage_trigger_draw`,
`soulbond_damage_trigger_draw`). Certificate `NotAssessed` (with what a bound must price). Before this,
Tandem Lookout's bare `soulbond` misrouted the deck to WhiteKnightsProvider; niv-core re-keyed the
WhiteKnights signature on Silverblade's payload (`9c9adecaa`).

## Claude-play sweep
- commit: `d09f881e7` (niv-aura; workflow `wf_64ca898c-6fa`, Opus players + an Opus skeptic per flag)
- seeds: 71000-71015 games: 16 (claude-play varies by SEED; AI benchmark = one pooled batch, d5 b20)
- flags: 3 unresolved (5 at the sweep; P1/P2 since FIXED -- `d9e002b8e`/`de36ef8ae`; the 3 left are the deferred low/cosmetic items below, awaiting the user's sign-off)
- Win turns: AI 7xT4 / 9xT5. Claude = AI on 12, FASTER on 3 (s71004, s71011, s71014: T4 vs T5),
  slower on 1 (s71006). s71004 / s71014 used exactly the payment shapes of P1/P2 below (a Servant-
  hasted Niv + Curiosity; a filter-fed Parun) -> acceptance checks for the payment fix; s71011's player
  reports the search still takes T5 at d5/b200 (an AI-misplay candidate to trace).
- 19 flags -> 15 upheld by the skeptic, 8 distinct defects:
  - FIXED -- **draw loop breadth-first** (s71008, s71009): Curiosity + a Lookout pair on one Niv drew
    ~2 cards per ping; the stack (LIFO) draws one. A 33-card library LOST where the real game wins
    with 18. Now an explicit LIFO trigger stack (RunDrawPingStack).
  - FIXED -- the Brainstorm put-back chooser was still ASKED after lethal in human play (s71003).
  - FIXED -- the dig decision's note said "hand index"; the reply indexes `examined` (s71010).
  - FIXED (`d9e002b8e`, MTG_PREPAY_FLOAT_POOL) P1 -- a floated sac-for-mana (Generator Servant) makes
    BatchPrepayMainCasts decline (PP_FLOAT_NZ), and the per-cast payer then strands a later cast:
    "hasty Niv + Curiosity on Niv" unreachable at any budget (s71009, s71013).
  - FIXED (`de36ef8ae`, MTG_RAMP_FEED_ROUTE) P2 -- the payer's scarcity pass never routes a generic through a
    ramp filter: Ferrous Lake land + Izzet Signet + a second spell unreachable (s71005).
  - UNRESOLVED, deferred (low) -- an emptied depletion land (Skerry / Needle) is sacrificed at the end
    of the plan / at SBA, not inside its mana ability; visible mid-plan (s71001, s71002). It could
    matter to a fastland played later in the same plan (it counts the dead land).
  - UNRESOLVED, deferred (cosmetic) -- combat still runs after a main-phase kill (s71006, s71009); the
    win turn is unchanged. A fix moves the post-kill log (= the digest) of every deck that kills in
    main 1 with no play change.
  - UNRESOLVED, deferred (cosmetic) -- Ponder's shuffle is logged "to the bottom" (s71003, s71005,
    s71009); the reveal folds into the play digest, so the fix rebaselines every Ponder deck's digest.
- Dismissed (4): the Ponder `ponder_keep` replay (protocol misuse), a "drops" plan (documented
  annotation), combat-after-lethal (one verifier: documented), a breakpoint-crossing prepay (documented).

## Open questions for the user (collected, never blocking)
- **Q1 (stack).** The AI skill asks that a stack-order-dependent card raise "does the stack need
  implementing first?". Self-targeted Remand is one. Default taken: no full stack — a combined
  "cast X, Remand X" action is faithful against a passive opponent (it never casts, so no other
  stack interaction exists). PROVISIONAL.
- **Q2 (Niv's "any target").** Always the opponent's face, the human is not asked (Pashalik /
  Omnath precedent; in a goldfish nothing else is worth hitting, and a 20-ping loop would be 20
  prompts). PROVISIONAL.
- **Q3 (Curiosity "you may draw").** Always drawn while the library is non-empty. PROVISIONAL.
- **Q4 (Remand is a play change for Hinata2).** Remand was never cast; self-Remand is legal, so
  Hinata2's play (and GT) will move. Tested as a play change (held-out A/B), not gated. niv-remand's
  probes: Hinata2 avg 5.775 vs 5.8125 (s7001 x80) and 5.65 vs 5.70 (s9001 x120); every slower game
  ties at budget 100; ~8% of games cast Remand; search units +12-16% at b10, one unbudgeted game 5.2x.
  Hinata2's keep table still LOADS (it carries its own name->bucket map; Remand stays in the inert
  bucket) and its value leaf predates the change -- both are now stale for the ~8% of Remand games.
- **Q5 (Aura host ranking -- a USER rule, extended).** The 2026-10-06 USER Aura host ranking (used by
  the plan dedup to keep ONE host per Aura cast) got a `combo` term after `lethal`: a damage->draw
  Aura on a pinging host ranks first. Without it the dedup kept Curiosity on a Goblin Guide and the
  kill was not in the plan list. 0 for every other Aura, so no other deck moves. Extension taken;
  the user owns the rule.
- **Q6 (soulbond pair-or-wait -- a USER rule, extended).** The 2026-10-06 USER rule ("pair-or-wait
  is searched when the only partners are below 2 power") now also emits the wait twin for a Tandem
  Lookout when no Niv can take the pairing (pairing a 2/1 Servant locks the Lookout away from a later
  Niv). `SoulbondPartner` ranks a Niv first (order only). Taken; the user owns the rule.
- **Q7 (second main ON).** `DeckUsesSecondMain` now fires for Curiosity / Lookout (combat damage
  draws a card, so a post-combat main has new resources -- analyze-deck 2c-bis). Main-1-vs-main-2 is
  the user's per-deck call; ON per the skill, hatch `MTG_NIV_SECOND_MAIN=0`. Cost to be measured.
- **Q8 (Brainstorm search cost).** Every distinct put-back is searched (median ~76 per plan, p90
  ~306), no width cap: a 4-Brainstorm test list searched ~4.5x slower at d3 b10 (avg win turn
  unchanged). This deck runs 2. The completeness rule forbids a lossy cap; if the suite cost gate
  (3x) binds, the options are a further SOUND fold or a budget-staged admission order -- to be
  proposed with numbers, not taken unilaterally.
- **Q10 (Remand at depth 0 -- a POLICY choice).** Hinata d0 got worse (6.791 -> 6.849 at s2002,
  71 slower / 16 faster): the depth-0 runner (a permitted greedy) is offered Remand riders, and its
  EvalCard prices Soulfire Eruption's dig and a turn-6+ creature at 0, so Remand's draw (100) outbids
  them (67 of 71 slower games have a Remand at the first divergence). Withholding riders from d0 is
  byte-identical to base. Options: (a) no riders at d0 (as the rollout leaf already does; gives up 16
  Irencrag-cycling wins, 5 of them unwon->won); (b) a per-deck provider rule (e.g. Hinata2 never
  Remands Soulfire / Hinata); (c) fix the greedy's valuation of Soulfire / late creatures (a
  heuristic change -- heuristic-optimization A/B); (d) keep. TAKEN: (d) pending the user (the
  rules-complete option set; d0 is not shipped play). The agent recommends (b) or (a).
  Its investigation also fixed three REAL defects where a countered X was still credited
  (`a21854b5f`): the leftover-mana FILL scaled a remanded Magma Opus (557x / 1000 d0 games; reached
  the search's no-rollout projected-win commit), Irencrag Feat counted X + Remand as one spell, and
  KillTriggerComboCredit counted a remanded burn spell. Hinata d0 2 faster / 1 slower, d3 identical.
- **Q11 (cantrip-first lets the Niv payoffs go first -- a CAST-ORDER change).** Searched play hoists
  plain cantrips to the front of a plan; `MayPrecedeCantrip` exempted only affordability (mana, cost
  reducers) and documented an empty VALUE slot for payoffs a cantrip fires. The two Nivs, Curiosity and
  Tandem Lookout now take that slot (by param), so a plan casts them before its cantrips, an Aura's
  same-plan host moving up with it. Without it the one-turn loop kill was unfindable at d3/d5. Cast order
  is the user's per-deck call; this is the documented design's default (no other deck carries these
  params), measured as part of the A/B above. TAKEN pending the user. **SIGNED OFF 2026-10-31 (USER), amended:**
  Curiosity / Lookout precede the cantrips only with a Niv in play or earlier in the plan -- see "Cast order".
- **Q8 update.** The niv-bs agent made Brainstorm's put-back pick ~2x cheaper (lossless) and adopted
  the nested overrun bail; the put-back PRICE (the budget-staged lever) measured worse and is OFF.
- **Q9 (breakpoint holes un-gated).** Three general search-completeness holes (a card drawn by an
  activation / trigger, esp. into an empty hand or in a fallback main, could not be cast that
  phase) are fixed for EVERY deck (`c52ed5c9d`), not just Niv. Expected to move Bruna, Snow and
  EldraziDisplacerFlicker play; measured as a play change.

- **Q12 (the two payment levers -- adoption is the user's).** `MTG_PREPAY_FLOAT_POOL` and
  `MTG_RAMP_FEED_ROUTE` ship DEFAULT ON (=0 restores each). Points to decide: (a) the ramp route also
  changes HUMAN play / the viewer's plan menus (the reference sweep did not move; autonomous-only is the
  alternative); (b) +4.7% CPU on a deck already over the 3x rule; (c) a scoring-vs-play gap it WIDENS:
  breakpoint continuations are paid one cast at a time in both worlds, while the search's scoring
  prepays continuation candidates -- it did for empty-float starts, now also for float starts. TAKEN:
  ON pending the user.
- **Q13 (s71014: a T1 land-order miss).** Claude's T4 kill (Ferrous Lake, Parun, crack the Servant)
  needs Saprazzan Skerry on T1; the AI plays Spirebluff Canal and is T5 at b20/b200/b2000. A search
  question (land choice), not payment -- open, to look at with next week's cantrip/heuristic work.

- **Q14 (Preordain's scry is a provider prune, not searched).** A cast scry resolves through the
  provider's `ScryKeepOnTop`; the searched scry axis covers land-ETB scries only. The no-greedy audit
  lists "cast-scry via ScryKeepOnTop" as a fine provider prune ("extend the scry axis to cast
  sources"). Two separate questions: (a) search it (up to 5 options per Preordain; moves Hinata2 too);
  (b) the rule it gets here is `GenericProvider`'s -- keep a land only while < 2 lands are in play --
  so from turn 3 every Preordain bottoms every land in a deck whose Nivs cost 6. A Niv
  `ScryKeepOnTop` is a provider heuristic (variants, held-out A/B, the user adopts). Neither taken;
  `docs/design/niv-cantrip-branching.md`.
- **Q15 (Niv's bottom-eval setting -- MULLIGAN policy, the user's).** With no keep table, every legal
  bottom subset plays a full game at play settings, and that is now the deck's dominant cost. The
  agent's held-out A/B vs current (d3 b10 x150 s61000 / d5 b20 x60 s62000): PD's `bottom_eval_budget_ms 1`
  d3 +0.013 (2 slower), 0.57x units / d5 +0.033 (1 slower), 0.49x -- the one lever that clears the 3x
  gate on its own (est. d5 s3003 ~7 s/game); Melira's (depth 0 + top-5 refine) +0.007 / +0.017, 0.77x /
  0.82x; budget 1 + top-3 refine +0.020 / -0.017, 0.76x / 0.77x. The keep table removes this cost
  entirely, but the suite gate comes before the keep table. NOT taken.
- **Q16 (the 25x overrun ceiling is global).** 45-99% of Niv's units sit in passes that ran to it and
  were thrown away. A settings question across every deck; no change proposed yet.
- **Q17 (value leaf, 2026-10-24 -- a cost/quality TRADE, the user's).** No candidate is cheaper than live
  play, so none was adopted: EMUL -0.0108t at 1.29x units (8/0 seeds), NLESC -0.0115t at 1.54x, NLFIT
  -0.0080t at 1.56x; the staged leaf itself is worse (+0.0035t) at 1.99x. Agent default: stay leafless (USER
  10-23: no compute budget this week); EMUL if the gain is wanted (copy command in the run's DECISIONS.txt,
  then re-run `valueleaf.sh run` for E.7 / E.7b / E.6).
- **Q15 is moot since the keep-table adoption (2026-10-24):** the table replaces the bottom-eval rollouts
  for every tabled hand, so the setting no longer matters for Niv.

## Approved deferrals
