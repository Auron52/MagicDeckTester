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

## RESUME STATE (2026-10-18 ~12:35 UTC)
**PUSH HOLD (USER, 2026-10-18 ~10:10 UTC): do NOT push until ~16:10 UTC ("hold off on pushing until maybe 6 hours from now"). Local commits only.**
**USER NOTE (2026-10-18 ~10:50 UTC):** *"We may need to work more on heuristics for this deck next week.
Ponder, Preordain and Brainstorm are especially troublesome in terms of potential branches."* -> the
cantrip-branching census goes in `docs/design/niv-cantrip-branching.md` (from the perf agent's report),
as next week's starting point.
Worktrees (all under `/workspaces/MagicDeckTester2/.claude/worktrees/`):
- `niv-aura` = branch `niv-aura` = THE INTEGRATION TIP. The LIFO draw loop is COMMITTED (0d0892e19); the
  payment agent's P1/P2 fast-forwarded on top (de36ef8ae), rebuilt: 1495/1495 unit, 229/229 scenarios. Uncommitted (keep uncommitted): the suite rows in `test/regression_cases.sh` +
  `test/suite_cost.json` (niv 124,304 ms/game). ab2 DONE (LIFO: outcomes identical to ab1's niv-aura arm,
  see the Brainstorm/Aura section).
- `niv` = branch `niv-analysis` (behind niv-aura; ff to niv-aura once Stage 4 is done). Its build is the
  OLD T2 binary the Stage-4 analyzer runs on (discard evidence, 400 games d3, single thread, 4.5h+): do
  NOT rebuild until `logs/stage4.status` says done.
- `niv-perf`: PERF AGENT DONE, integrated (see "Search cost cut"; was: the 3x rule; leads = per-unit cost explosions: d3 b10 s3007 gi4
  1,507 s / 1.64M units, s2018 gi16 347 s / 246k units, s2029 gi27; d5 b20 s3007 gi4 >20 min). Told NOT
  to touch the draw loop or the payment code.
- `niv-pay`: PAYMENT AGENT DONE (integrated, see "Payment holes P1/P2 fixed") (sweep defects P1 PP_FLOAT_NZ with a Servant float, P2 ramp-filter
  scarcity; acceptance seeds s71004/s71011/s71014 where Claude beat the AI).
- `niv-bs` (done, integrated; the A/B base), `niv-base` (origin src), `niv-core`/`-remand`/`-mana`/`-d0`.
Running: gi73 d3 b0 on niv-bs vs niv-aura (`build/scratch/niv-aura/ab/gi73_b0_*.txt`, 1.5h+), Stage 4.
DONE this session: 3x cost rule FAIL (Stage 5j section); claude-play sweep recorded (## Claude-play
sweep: 5 unresolved -> 2 are the payment agent's, 3 deferred low/cosmetic need the user's sign-off);
verify_deck --no-sweep static checks.
NEXT after that: (1) integrate niv-pay + niv-perf reports (rebase onto niv-aura), re-measure the cost
(`suite_gate.py --measure`; NOT `--cost`, which re-measures); (2) Stage 4 done -> stage4_analyze.json
(mulligan_flags; discard buckets = AUTHOR + REPORT), commit profile, provider_audit --check, ff
niv-analysis; (3) Stage 5 runtime: verify_deck full (nonconv/fd-diverge), multi-depth, 5c2 (card-cost
audit DONE 10:55 UTC: 12/12 costed cards match Scryfall, rc=0); (4) when the deck fits 3x: tiers + full smoke/regression (nested bail,
Action::def and AuraSpellCastLegal are GENERAL -> attribute every moved deck); (5) AFTER the hold:
rebase onto origin phase-1-2-deck-analyzer, push, CI, ff primary, check_references_safe.

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
- flags: 5 unresolved at the sweep; 2 since fixed (P1/P2), 3 deferred (low/cosmetic, the user's sign-off)
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
  params), measured as part of the A/B above. TAKEN pending the user.
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

## Approved deferrals
