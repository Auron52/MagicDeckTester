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

## RESUME STATE (2026-10-18 ~03:45 UTC, written before a context compaction)
Worktrees (all under `/workspaces/MagicDeckTester2/.claude/worktrees/`):
- `niv` = branch `niv-analysis` (integration; HEAD `0b955f153` + this note). Build there = T2
  (`c52ed5c9d` src); do NOT rebuild while the Stage-4 analyzer below is running off it.
- `niv-core`, `niv-remand`, `niv-mana`, `niv-d0`: merged into niv-analysis (keep; T1 binary = niv-mana).
- `niv-base`: detached at `42480e6c6` (origin src), built -- the BASE arm for attribution.
- `niv-bs`: AGENT RUNNING (Brainstorm put-back search cost: lossless memo, sound folds, a budget
  PRICE in the equip-route good shape; report pending). Integrate when it reports.
- `niv-aura`: UNCOMMITTED fix (TurnSolver.cpp + test_generator_servant.cpp, built, its unit test
  passes): `SubsetHasHostSacrificedForMana` -- reject a subset whose Aura spell / soulbond pairing
  targets a creature the same subset cracks for mana (Generator Servant; SacForMana is applied in the
  pre-pass, so the host is gone on resolution -> silent fallback; [enchant-retarget] read 3.7M on the
  analyzer pass). Identity/dominance fold, lockstep in Solve's consider + eval_and_push +
  RetargetSolveAuraHosts. Negative control running (`logs/arms/aura_niv*.txt`: 20 games d3 b10
  s5001, retarget count + win turns, niv vs niv-aura). Then commit + integrate.
Running: Stage 4 `logs/stage4.sh` (status `logs/stage4.status`; analyzer JSON
`logs/stage4_analyze.json`, err `logs/stage4_analyze.err`). Card scores DONE (2h, a slow tail of
games stuck in PutBackCandidates per perf), `decks/NivMizzetCombo/NivMizzetCombo.profile.json`
WRITTEN (uncommitted); the cost-aggregate diagnostic batch (reframe A/B, ~400 games d5/b20) is in
its own slow tail.
Aura negative control RESULT (20 games d3 b10 s5001): retargets 93,663 -> 15,352 (-84%, a SECOND path remains -- find it with MTG_ENCHANT_RETARGET_ABORT=1 on the niv-aura binary); one game moved, gi10 T4 -> T5 (an identity fold should only move play via budget redistribution -- re-run gi10 at --budget-ms 0 on both arms before committing).
NEXT, in order: (1) finish the aura fold (second path, gi10 at b0) -> commit `niv-aura`, ff into niv-analysis; (2) Stage 4
finishes -> read `stage4_analyze.json` (mulligan_flags, card scores), commit the profile;
`python3 scripts/provider_audit.py --check` (the deck is audited once it has a profile);
(3) integrate niv-bs; rebuild niv; (4) `python3 logs/add_niv_rows.py` (suite rows, all three tiers,
key `niv` + `niv2hg`), `python3 scripts/suite_gate.py --measure decks/NivMizzetCombo` -> the 3x
rule (budget 8086 ms/game = 3 x pd 2695). OVER 3x -> the deck goes in NO tier and getting it under
becomes the first goal (Brainstorm fan, second main) -- report to the user; (5) if in: run each
tier `--deck=niv,niv2hg` + full smoke/regression for the integrated tree, analyze every searched
slower game, accept (smoke/regression full; overnight --deck=niv,niv2hg only), check_gt_logs;
(6) Stage 5: verify_deck.py, nonconv/fd-diverge, multi-depth d0/3/5, 5c2 leaf_tiebreak_check,
5h audit_viewer_decisions (full sweep), 5d claude-play sweep (Opus/Sonnet fan-out ~15-20 games),
5i discard buckets (AUTHOR + REPORT, user approves); (7) push niv-analysis onto
phase-1-2-deck-analyzer (rebase first), watch CI, ff primary, check_references_safe.

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

### Stage 4a — provider
`NivMizzetComboProvider` (`067b3a1db`): an empty `DeckProvider` derivation (the deck rode Generic),
routed at the top of `SelectDecisionProvider` by four params OR'd across four cards no other deck
carries (`draw_trigger_damage`, `cast_trigger_instant_sorcery_draw`, `aura_damage_trigger_draw`,
`soulbond_damage_trigger_draw`). Certificate `NotAssessed` (with what a bound must price). Before this,
Tandem Lookout's bare `soulbond` misrouted the deck to WhiteKnightsProvider; niv-core re-keyed the
WhiteKnights signature on Silverblade's payload (`9c9adecaa`).

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

## Approved deferrals
