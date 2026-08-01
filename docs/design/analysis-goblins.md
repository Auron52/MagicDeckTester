# Analysis ledger — Goblins

Per-deck durable state for the `analyze-deck` workflow (survives compaction / handoff).
Deck: `decks/Goblins/Goblins.cod`. Branch: `phase-1-2-deck-analyzer`.

## ✅ OPTIMIZATION COMMITTED + PUSHED — 2026-07-31
Search-perf heuristics + GT re-accept are **committed and pushed** (branch `phase-1-2-deck-analyzer`;
`ec70359` heuristics+smoke-GT, `5e2a30f` regression-GT; rebased cleanly onto the remote's 23 B1-refactor
commits — GT held, no conflicts). Adopted: token-hash fix (~13%), idea 1 (haste-gated Skirk, GoblinsProvider),
idea 2 (board-lethal cut, **Goblins-only** via `UseLethalShortCircuit`), Mogg echo `PayEchoToKeep` (kept but
inert). Rejected: breadth cap + Matron narrowing (quality regressions). Net Goblins **regression tier all 5
configs IMPROVED** on held-out 2002/3003 (d0 4.8200→4.7530); non-Goblins byte-identical; 0 play-drift.
**Post-rebase quirk:** one of the 23 remote commits made the value leaf ACTIVE in the d5 smoke case (pre-rebase
it was leaf-independent) — with the untracked `value.json` present, local d5 = 4.3067 vs committed no-leaf GT
4.2933. Harmless: `value.json` is untracked, so a fresh checkout / CI uses the built-in default and smoke is
**27/27**. If d5 GT is ever re-accepted, do it with `value.json` moved aside (the committed state).
**Remaining follow-ups:** (1) overnight-tier GT re-accept (leaf-independent; the documented 8h tail).
(2) DEPTH MATRIX generation — IN PROGRESS (see below). (3) global lethal-cut eval — docs/design/lethal-short-circuit-global.md.

## 📊 DEPTH MATRIX — GENERATED + H3/H4 RESAMPLE RUNNING (2026-07-31)
Built the V1-8 × H1-7 fallback table (`logs/eval/goblins_depth_matrix.txt`; run log goblins_matrix_run.out).
Existing (stale) leaf used for the V-arm; the H-arm (user's emphasis) is pure rollout / leaf-INDEPENDENT.
**Results (mean over seeds 8008/9009):**
- **Heuristic (rollout):** H1=4.4625[53ms/400g] H2=4.4350[365ms/400g] H3=4.3800[3.4s/100g*] H4=4.3600[13.6s/100g*]
  H5=4.1000[22s/50g*] H6=4.1000[29s/50g*] H7=4.1000[28s/50g*].  (* = intractable → reference-only.)
- **Value leaf:** V1=4.8175 V2=4.7300 V3=4.5775 V4=4.4712 V5=4.4313[435ms/400g] V6=4.4437* V7=4.4150* V8=4.3800*.
- **Two findings:** (1) deep rollout keeps improving (H5-7≈4.10) but is 22-30 s/game → infeasible in play (that
  is WHY the leaf exists). H5-7 are 50g reference (noisy; the identical 4.10 is the small sample saturating).
  (2) **The current leaf is WEAK**: V5 (4.4313) ≈ H2 (4.4350) and is WORSE than H3 (4.3800) — searching to
  depth 5 with this leaf buys only ~depth-2 quality. A good leaf should push V5 toward H5(~4.10). This is a
  stale/under-trained leaf → **regenerating it would likely help** (deferred; user leaned "use existing").
- **H3/H4 RESAMPLE RUNNING** (user wants H3 full + H4 to 300-400g for the escalation decision — "is it safe to
  only escalate to H3?"): `python3 scripts/attic/valueleaf_depth_matrix.py --decks goblins --hdepths 3 4
  --vdepths 5 --seeds 8008 9009 --incremental --batch 50 --target 400 --reference-target 400
  --intractable-sec-per-game 30 --out logs/eval/goblins_h34.txt` (log: goblins_h34_run.out). ~1.5h (H4 =
  13.6s/game × 400g × 2 seeds). RESUMABLE. When done: read logs/eval/goblins_h34.txt for H3@400g / H4@400g and
  decide escalation depth (if H4 ≈ H3, cap at H3; if H4 shows the H5-ward jump, escalate to H4).

## ✅ MATRON open question — RESOLVED (NOT a clairvoyance artifact) — 2026-07-31
Per-game diff (RelWithDebInfo build, `MTG_GOBLIN_MATRON` flag, `MTG_VALUE_MODEL=0`, d3/400 seed 8000; logs in
logs/matron_on|off): narrowing ON=4.4375 vs OFF=4.4350 = exactly 3 games differ (217,259 SLOWER 5→6; 273
FASTER 5→4). The 2 slower are the **lone-Lackey exclusion misfiring**: game 217 opening has **Muxus** in hand,
game 259 has **Krenko+Warchief** — HEAD fetches Goblin Lackey T3, Lackey connects T4 and cheats the in-hand
BOMB into play → win T5. The line needs NO future knowledge (the bomb is visible in hand), so it is a REAL
fast line, not an oracle artifact. My static "lone Lackey = too slow" rule can't see the in-hand bomb. Verdict:
**rejection stands, understood** — leaving Matron to the search is correct (Lackey's value depends on what's in
hand to cheat, which a static list lacks). Investigation code reverted; HEAD clean.

## ⇩⇩ (historical) VALUE-LEAF + DEEP-SEARCH OPTIMIZATION (post-compaction) — 2026-07-31
**Task:** create the Goblins value-leaf / play profile. En route, user asked to OPTIMIZE the deep-search
first (the depth matrix's unbounded H-cells have extreme long tails). **Branch `phase-1-2-deck-analyzer`
@ `be7bc02`, in sync w/ origin.** Rebuild before using the binary (`bash build.sh`).

**★ KEY REFRAMING (2026-07-31): the "extremely long games" are the PURE-ROLLOUT arm, not shipped play.**
`UseValueModel()` is **ADOPTED default-ON** (DecisionProviders.cpp:130; the DecisionProvider.h:75 comment
saying "default OFF" is STALE) — the learned value leaf replaces the horizon rollout whenever a
`<deck>.value.json` sidecar is present. Controlled test, seed 9040, d5, same binary/flag/seed: **value.json
PRESENT → 6 s; value.json MOVED ASIDE → 560 s (~90×).** So the shipped `value_play` config is already fast;
the slow runs are the **depth-matrix's HEURISTIC cells** (value leaf OFF via `MTG_VALUE_MODEL=0`), which
exist to calibrate the leaf's fallback crossover. Optimizing the rollout arm = making those deep H-cells
tractable, NOT fixing shipped play.

**UNCOMMITTED WORKING TREE (survives compaction; nothing committed yet):**
- `src/cards/CardDatabase.h` — **DONE, verified byte-identical, ready to commit.** Token-hash fix: tokens
  aren't in the DB → `LookupCached` re-hashed the name + failed a full-table probe every call. Now caches
  the MISS via a `NotInDb()` sentinel. ~13% of the deep rollout. Smoke 27/27, 0 configs changed.
- **Idea 1 ADOPTED into `GoblinsProvider` (NEW class, DecisionProviders.{h,cpp}).** Hook
  `DeferSacOutletPreCombat(state, src, is_mana_outlet)` (DecisionProvider.h, non-pure default `false` →
  every non-Goblins deck byte-identical). Defers the VALUE sac outlets (Siege-Gang/Pashalik/burst) to the
  2nd main; **haste-gates Skirk's mana outlet** (keep pre-combat only if a Goblin haste lord is in play or
  castable from hand, else defer). ADOPTED default-ON, off-switch `MTG_NO_GOBLIN_SAC_2ND`. `TurnSolver.cpp`
  `CollectActions` now calls the hook (old root `MTG_GOBLIN_SAC_2ND` flag REMOVED). Verified equivalent:
  d3/400 (value.json aside) provider-ON=4.4350, provider-OFF(`MTG_NO_GOBLIN_SAC_2ND=1`)=4.4375 (=GT
  baseline) — matches the old flag-on/off exactly. `goblin` now routes to `g_goblins` (was `g_generic`).
- `scripts/attic/valueleaf_depth_matrix.py` + `valueleaf_table_to_metadata.py` — goblins registered.
- `decks/Goblins/Goblins.value.json` (untracked) — value model + enabled `value_play` (d5/b20). **STALE:
  pre-token-fix + pre-idea-1 binary. Must regenerate on the final binary.**

**IDEA 1 A/B (measured, quality-neutral, adopt-worthy):** d3/400 (rollout) 4.4375→4.4350 (−0.0025, noise);
every outlier wins the SAME turn. Speed on the pure-rollout d5 outliers: **8151 757→163 s (4.6×), 8021
363→132 s (2.75×), 8111 38→15 s (2.53×)** — big on Skirk-amplified seeds; **9040 624→560 s (1.11×), 9175
740→695 s (1.06×)** — near-flat on cast-subset-bound seeds (correct: those have a haste lord out, so idea 1
KEEPS Skirk). [Note: those raw d5 numbers were inflated by running 6 games in parallel; single-run is less,
but the RATIOS hold.]

**9040 EXAMPLE HAND (the case idea 1 doesn't speed — user asked to see it):** opening 2×Skirk / 3×Mountain
/ Goblin Warchief / Goblin Lackey. By T5 MAIN_1 the board is 2×Skirk, Lackey, **Warchief**, Stingscourger,
Aether Vial; opp at **7 life**; hand=1 card. With Warchief's +1/+0 anthem the 5 creatures swing for **11 ≥ 7
= ALREADY LETHAL with zero casts**, yet the rollout still enumerates ~2⁶ cast subsets at that node. Branch-
stats drivers (rollout, 212K EnumeratePlans): Goblin Warchief (max odo 64), Skirk (64), Lackey, Aether Vial,
Stingscourger. → this is a **lethal-board node** (idea 2 fires here) AND has legit build-up cast-subsets.

**RESIDUAL-TAIL LEVERS — RESOLVED (2026-07-31). User chose "most thorough" (idea2 + cap + Matron + Mogg);
outcome after A/B: 2 ADOPTED, 2 REJECTED (quality), 1 KEPT-but-inert.**
1. **Idea 2 — lethal-board short-circuit — ADOPTED but GOBLINS-ONLY (provider hook UseLethalShortCircuit,
   default false; GoblinsProvider returns true; off-switch MTG_NO_LETHAL_CUT).** In Solve (sibling of the
   combo-line/go-off cuts, ~TurnSolver.cpp:4064) AND EnumeratePlans (~:7920): if `pending_atk >= opp.life`,
   evaluate the empty (attack-only) subset via consider()/eval_and_push and return it, skipping the 2^m
   odometer. **WIN-TURN-invariant but NOT play-digest-invariant** — the smoke revealed it changes WHICH
   winning plan is chosen (skips pointless pre-lethal casts) on EVERY deck: all non-Goblins avgs identical
   but their play_digests differed (22 "fails", all same-avg). So it was gated Goblins-only to keep the 6
   other decks byte-identical; Goblins re-accepts its GT for idea 1 anyway, so the digest change is free
   there. Speedup MODEST (~7%: 9040 rollout 237→221 s) -- the cost is turn 3-4 BUILD-UP (non-lethal) nodes.
2. **Cast-subset breadth cap (CapGroupsBySituationalRank) — REJECTED.** GoblinsProvider EnumGroupCap +
   SituationalCardRank override. A/B: cap6 4.4375 (+0.0025), cap5 4.4400, cap4 4.5200 — NOT quality-neutral,
   AND ~0 speedup on 9040 (217/219/239 s). A static rank can't capture Goblin card value (too deep). A
   non-neutral cap would also corrupt the depth-matrix's heuristic-arm reference. Overrides REVERTED.
3. **Goblin Matron tutor exclusion (Pashalik/King-when-Chieftain/lone-Lackey) — REVERTED, but REJECTION IS
   AN OPEN QUESTION (revisit after the matrix).** A/B: narrowing ON 4.4375 vs OFF 4.4350 (+0.0025) — the
   clairvoyant search finds a line through an "excluded" target. Code reverted so HEAD is clean. BUT the
   +0.0025 may be a **clairvoyance ARTIFACT**, not a real loss: the regression GT is the CLAIRVOYANT search's
   output, so pruning a line the oracle only found because it knew the deck order reads as a "regression"
   even if it improves real (non-clairvoyant) play — the same tension as the fd-oracle "faster line greedy
   can't reproduce" finding. TODO (post-matrix, user-requested): diff per-game win-turns (narrow OFF vs ON,
   d3/400 seed 8000) to pinpoint the ~few regressed games; for each, inspect via `test/explain_game.py` /
   claude-play whether the lost line DEPENDS on knowing the future draw (artifact -> narrowing is actually
   correct for real play -> reconsider adopting) or is a blind-reproducible line (real loss -> stay reverted).
   The narrowing lives (reverted) in git history / this session for easy re-apply. See heuristic-optimization
   skill (clairvoyance-vs-artifact attribution).
4. **Mogg War Marshal echo keep-exception — KEPT but MEASUREMENT-INERT.** Provider hook `PayEchoToKeep`
   (DecisionProvider.h base = old fixed heuristic verbatim → all decks byte-identical; GoblinsProvider
   overrides). Both echo sites (AIEngine.cpp:~1218 executor + TurnSolver.cpp:~6825 rollout) call the hook
   → lockstep by construction. Rule: a self-token body (Mogg) PAYS echo (keeps the live attacker) when
   lethal THIS turn (the death token is summoning-SICK) OR no other castable spell ("no gas"); else
   declines (default). Correct + never-worse + user-requested, but **d3/2000 ON=OFF=4.3930 (0 change)** —
   the scenario ~never arises in random goldfish play (you're usually at overkill or win next turn anyway).
   Off-switch MTG_NO_GOBLIN_ECHO. Kept as a correctness safety net (may matter in claude-play / edge lines).

**ALSO: token-hash fix (CardDatabase.h) still uncommitted + ready (~13% of the deep rollout).**
**Net Goblins d3/400 rollout: 4.4375 (all-off) → 4.4350 (all-on).** Non-Goblins byte-identical (smoke
verifying at time of writing; idea 2 is the only generic change and it is GT-invariant by construction).

**PIPELINE ORDER (do NOT skip):** freeze ALL heuristics FIRST → rebuild → **regenerate the ENTIRE value-leaf
pipeline** (rows+train+matrix+gate+A/B) on the final binary (heuristics change `CollectActions` → shift
rows/matrix/play at ALL depths) → adopt value_play → **rebaseline GT** (idea 1 shifts d3 AND d5 coverage;
the value leaf shifts the d5 value_play case) across all 3 tiers → commit (token fix can commit now) + push.
Value-leaf pipeline cmds retained in the section below.

## ✅ REBASE-THEN-PUSH — DONE — 2026-07-30
**Goblins work is rebased onto `origin/phase-1-2-deck-analyzer` (tip `eb29a90`) and verified.** No merge commit (linear rebase, per user).
- **Rebase:** 20 commits replayed cleanly onto `eb29a90` (0 behind after). Conflicts resolved with `rerere`:
  - **EffectiveCost (structural):** the remote unified both `EffectiveCost` twins into one shared `EffectiveSpellCost` (ManaPayment.cpp). Kept HEAD's delegation in AIEngine.cpp + TurnSolver.cpp and **moved my Goblin Warchief `reduces_spell_subtype` reduction into `EffectiveSpellCost`** (single home, still gated → non-Goblin decks byte-identical).
  - **Suite (additive):** the remote added deck **`auras`** in the same slots where I added **`goblins`** — kept BOTH in regression_cases.sh (DECK_FILE/DECK_PROF/SMOKE/REGRESSION/OVERNIGHT). In regression_gt.txt kept the remote's **post-Orchard-fix** values for the shared decks (hinata/dragonstorm/antilife/auras) and appended my goblins entries.
- **Build break fixed:** the remote renamed `AIEngine::BuildAvailableMana` → free fn `AvailableManaPool(state)`; updated my 3 echo/upkeep call sites.
- **Verified:** full `--smoke` = **27/27 PASS byte-identical** (all decks incl. goblins d0=4.81/d3=4.29/d5=4.27; per-game audit 0 play-changed; viewer protocol 138 ok / 0 drift). Goblins GT held on the rebased binary (Orchard/land-drop refactor did NOT shift Goblins play → no re-accept needed). fd-oracle+nonconv spot-check (seeds 1001/2011/4004/6006 @ d5) = **0 diverge / 0 nonconv** (the old benign seed-2011 off-by-one no longer reproduces — plausibly closed by the remote land-drop fix).
- **Safety backup** `goblins-work-backup` @ pre-rebase `ce0979f` retained until push confirmed.

## ✅ ANALYSIS COMPLETE — 2026-07-30 (Stage 6)
**Goblins deck analysis is DONE through Stage 6.** Engine complete; profile committed; all verification passed; both viewer choosers wired+proven; multi-sac refinement in; deck added to the regression suite with accepted baseline GT.
- **Win-rate:** ~98% (avg **4.36** turns / 300 games at default policy). Smoke GT: d0=4.81, d3=4.29, d5=4.27.
- **Stage 5a** — nonconv 7/7 seeds CLEAN; fd-diverge = ONE (seed 2011, off-by-one) root-caused as a benign clairvoyant-rollout-vs-greedy search-optimality gap in CORE search (combat math lockstep-shared via `ApplyAttackSelfPumps`; executor play verified correct; controls Burn/Slivers/Knights/Dragons 0/200) — NOT a Goblin bug.
- **Stage 5d** — 26 claude-play games CLEAN (0 flags, 0 misplays; search==Claude on win turn). Sweep capped ≤20 on Sonnet (skills updated).
- **Stage 5h** — both viewer choosers wired + PROVEN firing: `lackey_put` (Goblin Lackey put-from-hand; seed 9201 → candidates [Twinshot,Muxus,Matron], default Muxus) and `echo` (Mogg/Stingscourger pay-or-sac; seed 2011 → Mogg {1}{R} default decline). GT byte-identical (choosers null unless `--claude-play`). All 44 new params classified in `audit_viewer_decisions.py`. Residual auditor HARD MISS on lackey_put/target = forcing limitation (conditional triggers unreached under AI-optimal play), disproven as dead-chooser by the engineered repros.
- **Multi-sac refinement** — bounded burst action (`sac_count=k`, Siege-Gang swarm-sac lethal); lockstep-clean (fd-diverge only the pre-existing 2011; nonconv 0), gated (dragonstorm smoke_d3 = 4.6467 unchanged), no hang.
- **Field audit** — `audit_card_fields.py` fixed (echo/channel strip; MDFC front-face); Piledriver protection-from-blue allowlisted (goldfish-inert); snapshot re-fetched (Scryfall 429s cleared).
- **Regression suite** — Goblins added to all 3 modes; smoke baseline GT accepted; regression-mode baseline running; **overnight baseline is the one remaining long follow-up (8h)**.
**Commits (branch `phase-1-2-deck-analyzer`):** skills 1254ab4; 5a/5d ledger 913d1ee; choosers 8ef3627; multi-sac 6c118be; suite+GT be1cb8a (+ snapshot commit pending).

## ⇩ RESUME HERE (post-compaction) — updated 2026-07-30
**State:** Goblin engine FUNCTIONALLY COMPLETE + Stage-4 baseline profile done. All work COMMITTED (branch `phase-1-2-deck-analyzer`, head `5772733`, ~8 goblins commits on top of `2b32beb`). Build is current: `bash build.sh` (NOT raw cmake; NO timeouts — repo rule). Binary `build/Release/mtg`; profile `decks/Goblins/Goblins.profile.json`.
**Done:** Stages 1–3 (coverage `missing:[]`); all 17 cards.json entries (costs Scryfall-verified — audit "All match", only Twinshot/Stingscourger 429-skipped but match research); every engine subsystem (params+parser, Warchief `reduces_spell_subtype`, ETB cascade `OnGoblinEnters`/Muxus reveal, death-watcher `OnCreatureDies`, echo, combat pumps Piledriver/Muxus, Goblin Lackey combat-cheat + `DeckUsesSecondMain`, Three Tree scaled mana, provider routing guard, Krenko `TapForTokens` lockstep, costed outlets Siege-Gang/Pashalik/Channel, Skirk mana via SacForMana-reuse). Stage-4 profile + Stage-5b multi-depth sanity PASS (d0=5.10, d3=4.85, d5=4.85 @seed6006 ×20; fd-diverge=0, nonconv=0 on samples). All params gated → other decks byte-identical (Lotus/Dragonstorm `ApplySacForMana` victim_id=0 path unchanged).
**Stage 5 progress (2026-07-30 session):**
- **5a DONE.** nonconv 7/7 seeds CLEAN (50 games/seed @ depth 3). fd-oracle @ depth 5 flagged exactly ONE: `seed=2011 realized_win=6 predicted_win=5 proven_at_turn=1` (reproduces deterministically). **Root-caused → NOT a bug (disclose):** the executor's real play is verified correct (T5 combat = 12 dmg, opp→2; the 2nd Mogg-death token from the T5 echo-lapse is correctly summoning-sick and doesn't attack), and combat math is **lockstep-shared** — `ApplyAttackSelfPumps` (Piledriver/Muxus pump) lives in SpellEffects.h and is called by BOTH TurnSolver::SimulateCombat and GameEngine::CombatPhase; both gate attackers via CanAttackFull. So this is the clairvoyant full-depth rollout proving a 1-turn-faster line at turn 1 that greedy per-turn execution doesn't reproduce — a **search-optimality gap in CORE search (all decks), not Goblin machinery**. Controls (Burn/Slivers/Knights/Dragons) = 0 fd-diverge / 200 games; Goblins rate low (measuring, seed 1001 = 0/25). Any fix would be to shared commit-the-line logic (regression-risky, out of scope for finishing Goblins).
- **5d DONE.** claude-play sweep: **26 games CLEAN** (base seed 8008, gi 0–26 minus a couple; 0 flags, 0 misplay candidates, search==Claude win turn 4 on every game). Sweep now capped at ~15-20 games on **Sonnet** (user directive; both skills updated + `logs/goblins_stage5d/sweep.wf.js` uses `model:'sonnet'`). Record for `claude_sweep` gate: commit `<HEAD>`, seeds 8008, games 26, **flags: 0 unresolved**.
- **Field audit:** `audit_card_fields.py` fixed (add `echo`+`channel` to MODELED_ELSEWHERE_KEYWORDS — modeled via params not tags; MDFC front-face type parse for `//` type lines, fixes Branchloft). Snapshot `--update` re-run but Scryfall **429-blocked on 3 newest Goblin cards** (Twinshot Sniper, Stingscourger, Three Tree City still UNFETCHED) — retry when rate limit clears; all fetched hard fields MATCH.
**NEXT STEPS (in order):**
1. **Finish field-audit snapshot** — re-run `python scripts/audit_card_fields.py --update` until Twinshot/Stingscourger/Three Tree City fetch (429s clear), confirm `audit_card_fields.py` exits 0, then COMMIT `scryfall_reference.json` + the auditor fix together.
2. **Stage 5h + viewer bucket-B** (NEEDS A REBUILD — do only when no sweep/oracle run is using the binary) — Goblin Lackey's "which Goblin permanent to put from hand" chooser is NOT wired (heuristic pick is faithful: highest-MV, in `FireCombatDamageCheatIntoPlay` SpellEffects.h:2028). Build per tools/play/DECISIONS.md 4-site pattern (new bucket-B type, model on `dragon`/`lightpaws`); then `python scripts/audit_viewer_decisions.py decks/Goblins/Goblins.cod`.
3. **Refinement (documented, non-blocking):** multi-sac-for-lethal as a searched COUNT (Siege-Gang saccing the whole swarm — currently ONE victim/activation via the hang-fix bound).
4. **Stage 6 report** to user: cards+tiers, profile, suite win-rate (run regression suite), Stage-5 outcomes above, **Stage-6a disclosure** (inert collapses: mountainwalk/protection-from-blue/first-strike/reach/Stingscourger-bounce/red-token-color; bounded sac-victim heuristic; single-activation sac cap; Three-Tree any-creature simplification; **the seed-2011 off-by-one fd-diverge search-optimality gap**).
**Patches from the parallel workflow archived at** `logs/goblins_patches/*.patch` (already applied+committed). User decisions: full faithful build / model echo / implement Three Tree scaled mana / inert collapses approved; **claude-play sweeps ≤20 games on Sonnet**.


## Deck list (25 distinct)
Muxus Goblin Grandee ×3, Rundvelt Hordemaster ×3, Twinshot Sniper ×1, Siege-Gang Commander ×4,
Goblin Lackey ×4, Goblin Piledriver ×4, Goblin Matron ×2, Goblin King ×2, Goblin Chieftain ×2,
Lightning Bolt ×2, Mogg War Marshal ×1, Goblin Warchief ×1, Mountain ×21, Aether Vial ×2,
Skirk Prospector ×2, Goblin Chainwhirler ×1, Stingscourger ×1, Krenko Mob Boss ×1,
Three Tree City ×1, Cavern of Souls ×1, Pashalik Mons ×1.
Sideboard (not analyzed): Experimental Frenzy, Lightning Bolt.

## Stage 1 — Coverage (2026-07-28)
Already covered: Lightning Bolt (full), Mountain (full), Aether Vial (full), Cavern of Souls (full).
**17 missing**, all Goblin cards + 1 land:
Muxus, Rundvelt Hordemaster, Twinshot Sniper, Siege-Gang Commander, Goblin Lackey, Goblin Piledriver,
Goblin Matron, Goblin King, Goblin Chieftain, Mogg War Marshal, Goblin Warchief, Skirk Prospector,
Goblin Chainwhirler, Stingscourger, Krenko Mob Boss, Three Tree City, Pashalik Mons.

## New engine infrastructure this deck needs (design in progress)
- ETB create-N-tokens (fixed): Siege-Gang (3× 1/1), Mogg War Marshal (1×). *(no existing ETB-flat-token param)*
- Death trigger: goblin dies → deal 1 dmg (Pashalik Mons); → make token (Rundvelt Hordemaster).
- Sacrifice-a-Goblin outlets: → add {R} (Skirk Prospector); → deal 2 any target (Siege-Gang, Pashalik).
- Tap → make X tokens (X = #Goblins): Krenko.
- Subtype tutor to hand: Goblin Matron (search Goblin card → hand). *(existing tutor is by card TYPE)*
- Cost reducer by subtype + haste grant: Goblin Warchief (Goblins cost {1} less, have haste).
- Combat pump per other attacking matching creature: Goblin Piledriver (+2/+0 each other attacking Goblin).
- Cheat-into-play on combat damage: Goblin Lackey (put a Goblin permanent from hand into play).
- Reveal top N, put matching (MV≤5) onto battlefield: Muxus.
- ETB ping each opponent creature + player: Goblin Chainwhirler (1 dmg).
- Channel (discard from hand → deal 2): Twinshot Sniper.
- Echo: Mogg War Marshal, Stingscourger.
- Lord + haste: Goblin Chieftain (existing grants_haste + lord_effect).
- Lord + mountainwalk: Goblin King (mountainwalk evasion inert vs passive opp; +1/+1 modelled).
- ETB bounce opponent creature: Stingscourger (mostly inert vs passive opp).

## Stage 2 research — collected drafts (6/8 families in)

### Tier 1 (cards.json only, existing params)
- **Goblin King** {1}{R}{R} 2/2 — lord_effect Goblin +1/+1, lord_excludes_self. Mountainwalk INERT (evasion vs non-blocking opp).
- **Goblin Chieftain** {1}{R}{R} 2/2 Haste — lord_effect Goblin +1/+1 + grants_haste + lord_excludes_self.

### Tier 2 (one small new param each)
- **Goblin Warchief** {1}{R}{R} 2/2 (Goblin Warrior) — lord_effect + grants_haste + NEW `reduces_spell_subtype:"Goblin"` (subtype twin of reduces_spell_color; mirror at all reduces_spell_color sites). Goblins cost {1} less, have haste.
- **Goblin Piledriver** {1}{R} 1/2 (Goblin Warrior, Protection) — NEW `attack_pump_power_per_other_matching:2` over subtypes_affected=["Goblin"] (mirror attack_trigger_life_loss scan, self-excluded, +power at declare-attackers both worlds). Protection-from-blue INERT.
- **Krenko, Mob Boss** {2}{R}{R} 3/3 Legendary (Goblin Warrior) — NEW tap-activated (no mana) `tap_creates_tokens_per_controlled_subtype:"Goblin"` + tap_created_token_power/tough/subtypes. X = #Goblins at resolution (incl. self+tokens). New Action::Kind. Summoning-sick gating via CanTap.
- **Skirk Prospector** {R} 1/1 — NEW `sac_subtype_for_mana_amount:1`+`_color:"R"`+`_subtype:"Goblin"`. No-tap, repeatable, sac any Goblin (incl self) → add {R}. New Action::Kind (contrast Lotus `sac_for_mana_amount`=tap+sac-self).

### Tier 3 (new engine machinery)
- **Mogg War Marshal** {1}{R} 1/1 (Goblin Warrior, Echo) — NEW `etb_self_creates_tokens:1` (+ reuse etb_created_token_*), NEW `death_creates_tokens:1` (+death_token_*), NEW `echo_cost:"{1}{R}"` (upkeep pay-or-sac decision). Not paying echo → death token (net same body, saves mana).
- **Siege-Gang Commander** {3}{R}{R} 2/2 — NEW `etb_self_creates_tokens:3`, NEW sac-outlet `sac_damage_cost:"{1}{R}"`+`sac_subtype_damage:2`+`sac_damage_requires_subtype:"Goblin"`, targeting Any. Repeatable sac-a-Goblin → 2 dmg (face in goldfish) = burn engine. New Action::Kind.
- **Goblin Matron** {2}{R} 1/1 — reuses tutor_to_hand + tutor_types:["Goblin"] (subtype match via CardMatchesTypeName fallback) + tutor_shuffle_after, BUT needs NEW **ETB-tutor dispatch** (PerformTutor currently spell-only; wire at creature-ETB in executor + rollout). Tutor target = search/viewer decision.
- **Goblin Lackey** {R} 1/1 — oracle is "deals damage to a player" (modern; no "combat"). NEW `combat_damage_puts_subtype_from_hand:["Goblin"]` — combat-damage trigger → put a Goblin permanent from hand onto battlefield (shared enter cascade). **Needs DeckUsesSecondMain += this flag** (2c-bis resource-in-combat). Bucket-B viewer chooser (which Goblin / decline).
- **Muxus, Goblin Grandee** {4}{R}{R} **4/4 Legendary (Goblin Noble), NO Menace** — NEW `etb_reveal_count:6` + `etb_reveal_put_subtypes:["Goblin"]` + `etb_reveal_put_creatures_only:true` + `etb_reveal_put_max_mv:5` (reveal top 6, put Goblin creatures MV≤5 onto bf via shared cascade, rest to bottom), NEW `attack_self_pump_per_other_subtype:"Goblin"`+power/tough:1 (attack +1/+1 per other Goblin). Not second-main-relevant. No viewer choice (puts ALL matching).

### Tier 3 (cont.) — damage/channel/death families
- **Goblin Chainwhirler** {R}{R}{R} 3/3 First strike (Goblin Warrior) — NEW `etb_damage_each_opponent:1` (ETB 1 to opp face + each opp creature/pw; face is race-relevant, AoE only matters vs spawn tokens). First strike INERT.
- **Twinshot Sniper** {3}{R} **2/3 Artifact Creature** (Goblin Archer), Reach+Channel — NEW `etb_damage_any:2` (ETB 2 to face) + NEW `channel_cost:"{1}{R}"`/`channel_damage:2` (from-HAND discard-activated burn = new hand-action). Reach INERT.
- **Stingscourger** {1}{R} 2/2 (Goblin Warrior), Echo {3}{R} — ETB bounce opp creature = GOLDFISH-INERT; Echo {3}{R} = pay-or-sac upkeep (model or defer, USER DECISION).
- **Pashalik Mons** {2}{R} 2/2 Legendary (Goblin Warrior) — NEW death trigger `dies_trigger_subtype:"Goblin"`+`_includes_self:true`+`dies_trigger_damage:1` (per Goblin death incl. own → 1 to face); NEW sac-outlet `{3}{R}` sac-a-Goblin → create TWO 1/1 Goblins (NO damage rider).
- **Rundvelt Hordemaster** {1}{R} 1/1 (Goblin Warrior) — lord_effect Goblin +1/+1 (lord_excludes_self) + NEW death-triggered impulse-exile (`dies_trigger_impulse_exile`: exile top on Goblin death; if Goblin creature, castable until end of NEXT turn). Lord is immediate/faithful; impulse-exile is the complex edge.
- **Three Tree City** Legendary Land — `{T}: Add {C}` faithful (produces ["C"]); clause 3 `{2},{T}: add N colored = creatures of chosen type` = board-scaled ramp/fixing, NOT modelled by default (under-rates as colorless-only). USER DECISION. ETB type-choice simplified to any-creature (Cavern precedent).

## CRITICAL STRUCTURAL INSIGHT (death-trigger agent)
Against the **passive goldfish opponent, our Goblins never die in combat** — deaths occur ONLY via the sacrifice outlets (Skirk Prospector, Siege-Gang, Pashalik, and Mogg-War-Marshal-lets-echo-lapse). So the death-trigger engine (Pashalik ping, Rundvelt impulse, Mogg/Rundvelt death tokens) is productive ONLY if the sac outlets are modelled. Build death-triggers + creature-sac-outlets as ONE coordinated subsystem, not per-card.

## Proposed deferrals — NEED USER APPROVAL (per skill 2a)
- Inert keyword/ability collapses vs passive opponent (standard): Goblin King mountainwalk, Piledriver protection-from-blue, Chainwhirler first strike, Twinshot reach, Stingscourger ETB bounce. Token "red" color unmodelled (nothing keys on it).
- Echo (Mogg War Marshal, Stingscourger): model as upkeep pay-or-sac decision, or defer as vanilla body (over-rating).
- Three Tree City clause 3 (board-scaled colored mana): implement or defer (under-rate as {C}-only).
- Rundvelt clause 2 impulse-exile, Pashalik sac-outlet: build now or defer edges.

## KEY INTEGRATION RISKS (found during infra survey)
1. **Provider misrouting:** `SelectDecisionProvider` sets `anti=true` if `p.tutor_to_hand` → Goblin Matron would route the whole deck to **AntiLifegainProvider**. MUST guard: detect Goblins (by a goblin-specific param) and route to Generic (or a new GoblinProvider) BEFORE the anti check, OR gate the anti tutor-signal. Same-shape block at DecisionProviders.cpp ~2278 and GoldFishRunner DeckUsesSecondMain.
2. **etb_self_creates_tokens** shared by Mogg War Marshal + Siege-Gang; reuse existing etb_created_token_power/tough/subtypes (Lathliss) — a card sets exactly one ETB-token gate, no conflict.
3. **New Action::Kind** values needed: SacGoblinForMana (Skirk), SacGoblinForDamage (Siege-Gang, Pashalik), TapForTokens (Krenko). Model on SacForMana precedent (TurnSolver.h Kind enum). Each needs: enumeration in CollectActions, cost/effect in apply_one (rollout) + executor, plan_signature inclusion.
4. **Death-trigger machinery** does not exist — CheckStateBasedActions (GameEngine.cpp:602) moves dead creatures to graveyard with no trigger hook. Need a "goblin died" event fired from BOTH the executor SBA and the rollout death path, driving death_creates_tokens (Rundvelt, Mogg) + death-damage (Pashalik). This is the biggest new subsystem.
5. **Echo** (Mogg War Marshal, Stingscourger) = new upkeep pay-or-sac decision.
6. **Second main** for Goblin Lackey via DeckUsesSecondMain extension.

## Implementation progress (serial integration)
- [x] CardParams fields added (CardDatabase.h) — full Goblins block. **Compiles clean (build exit 0).**
- [x] BuildParamsFromJson reads added (CardDatabase.cpp). Compiles clean.
- [x] reduces_spell_subtype (Warchief) — DONE, builds clean. Wired: TurnSolver EffectiveCost subtype block; AIEngine EffectiveCost copy; SameTurnReducerGenericCredit (with self-exclusion — Warchief is a Goblin); CheckLine in-order walk (sub_reducers); GenericProvider::CastOrderRank rank 8 (before creatures). All gated on non-empty reduces_spell_subtype.
- [x] ETB effects — DONE, builds clean. New `OnGoblinEnters()` + `PerformMuxusReveal()` in SpellEffects.h; called from EffectHandler::EnterBattlefield (executor, passes entry.tutor_target) + TurnSolver apply_one creature-enter (rollout, passes tutor_target). Handles etb_self_creates_tokens, etb_damage_any + etb_damage_each_opponent (face via life-loss; opp creatures pinged+pruned inline), Matron ETB tutor (PerformTutor), Muxus reveal-6-put-creatures-MV≤5 (via DrawN, rest to bottom, each put fires its own OnGoblinEnters cascade). NOTE refinement: Matron search-branching over tutor target relies on existing tutor_to_hand CollectActions enumeration — verify composes in Stage 5.
- [x] Death-watcher engine — DONE, builds clean. `OnCreatureDies(state, controller, dead_card)` in SpellEffects.h: scans other in-play watchers (subtype match) + the dead creature's own watcher (includes_self); applies dies_trigger_damage (face), dies_trigger_creates_tokens, dies_trigger_impulse_exile (stage top if type+subtype match, expiry turn+1). Wired into GameEngine::CheckStateBasedActions (collects deaths, fires AFTER erase loop to avoid iterator invalidation from token creation). PRIMARY death path = the sac outlets (next) which will call OnCreatureDies directly in both worlds. Documented approximation: simultaneous multi-death (unreachable in goldfish).
- [~] Sac-outlet subsystem — PARTIAL (this is the hard serial piece). DONE: Action::Kind values (TapForTokens, SacCreatureOutlet, Channel) + sac_victim_id field in TurnSolver.h; shared apply helpers in SpellEffects.h (CountControlledSubtype, ApplyTapForTokens, ApplySacCreatureOutlet [sacs victim→payload+OnCreatureDies], ApplyChannel); Krenko enumeration in CollectActions; Krenko rollout apply (trailing pass after apply_plan_actions, so X counts developed board); plan_signature cases for all three kinds.
  REMAINING (before Stage 5 / regression baseline):
  1. [x] **Executor Krenko apply** — DONE, builds clean. Trailing TapForTokens pass in AIEngine::TakeTurn at the mirrored post-cast point (before deferred Karoo, ~line 2448), lockstep with rollout ApplyPlanDirect ~5407. Krenko now fires identically in both worlds.
  2. [x] **Costed outlets** (Siege-Gang damage / Pashalik tokens) + **Channel** — DONE. Enumerated with real mana cost; apply pays via TapForCostDirect (rollout) / BuildAvailableMana+TapForCost (executor) in a trailing pass; stranded=no-op both worlds (no phantom fd-diverge). **HANG FIX**: bounded to ONE heuristic victim per outlet (token first / weakest / source last) — one-action-per-victim exploded the O(2^n) subset search. Commits cd47f29, 33715bd.
  3. [x] **Skirk mana outlet** (sac Goblin → {R}) — DONE. Emitted as a SacForMana action (reuses subset credit / BatchPrepay decline / pre-cast float / plan signature); ApplySacForMana gained a victim_id param (source stays, victim sacrificed). Lotus victim_id=0 byte-identical. Commit ad59f2f. fd-diverge + nonconv clean.

## ENGINE COMPLETE (functionally). Smoke: d3 seed2002 = 4.80; d5 seed4004 = 4.80; fd-diverge=0, nonconv=0 on samples.
Remaining refinements (documented, non-blocking): multi-sac-for-lethal as a searched COUNT (Siege-Gang saccing the whole swarm) — currently one sac/activation; viewer bucket-B wiring (Lackey put-from-hand chooser); field-audit snapshot (was 429-throttled).
- [ ] Echo upkeep pay-or-sac (needs Permanent flag entered-since-last-upkeep + upkeep step hook).
- [ ] Combat pumps: attack_pump_power_per_other_matching (Piledriver), attack_self_pump_per_other_subtype (Muxus) at declare-attackers both worlds.
- [ ] Goblin Lackey combat-damage-cheat + DeckUsesSecondMain extension + viewer bucket-B chooser.
- [ ] Channel (Twinshot from-hand action).
- [ ] Three Tree City scaled mana ({2} feeder → N colored = creatures).
- [ ] Provider routing guard (Matron tutor_to_hand must NOT route deck to g_antilife).
- [ ] Viewer wiring (Lackey put-from-hand; sac-outlet target; Krenko/echo choices).
- [ ] cards.json entries (17) + audits + coverage + profile + Stage 5.

## Parallel workflow (wf_e288902a-c63) — INTEGRATED 2026-07-29
All 5 worktree agents built clean; diffs applied via `git apply --3way` onto the Krenko commit (no conflicts); combined tree builds clean; coverage `missing:[]`; deck runs (3 games d3 → ~4.3 avg win turn). Patches saved under logs/goblins_patches/.
- [x] Echo (Permanent::echo_resolved + upkeep pay-or-sac in AIEngine::TakeTurn + rollout SimulateEndAndStartNextTurn; Mogg declines→death token, Stingscourger pays-or-sacs).
- [x] Combat pumps (Piledriver attack_pump_power_per_other_matching; Muxus attack_self_pump) + Goblin Lackey combat-damage cheat + DeckUsesSecondMain extension.
- [x] Three Tree City scaled mana ({2},{T} → N colored = creatures of type).
- [x] Provider routing guard (Goblins → GenericProvider before anti; Matron tutor no longer misroutes).
- [x] cards.json — all 17 entries (costs Scryfall-verified; param keys match parser).

## Status
- [x] Stage 1 coverage
- [x] Stage 2 research (fan-out) — 8 agents, authoritative Scryfall drafts collected
- [x] Stage 3 coverage clean (missing:[])
- [~] Stage 2 integration — 8 subsystems in; REMAINING: costed sac outlets (Siege-Gang/Pashalik/Channel) + Skirk mana + executor Krenko apply; viewer bucket-B wiring
- [ ] Stage 2d/2d-bis audits (cost audit running)
- [ ] Stage 4 baseline profile
- [ ] Stage 5 verify
- [ ] Stage 2d / 2d-bis audits
- [ ] Stage 3 coverage clean
- [ ] Stage 4 baseline profile
- [ ] Stage 5 verify
- [ ] Stage 6 report

## User decisions (2026-07-28)
1. **Full faithful build** — implement every clause including Rundvelt impulse-exile, Pashalik sac-outlet, Three Tree City scaled mana. No feature deferrals.
2. **Model echo faithfully** — upkeep pay-{cost}-or-sacrifice decision (Mogg War Marshal, Stingscourger).
3. **Implement Three Tree City clause 3** — board-count-scaled colored mana ({2} feeder → N of chosen color = creatures of chosen type).
4. **Inert collapses approved** — mountainwalk (Goblin King), protection-from-blue (Piledriver), first strike (Chainwhirler), reach (Twinshot), Stingscourger ETB bounce; token "red" color unmodelled. All disclosed as cosmetic vs the passive opponent; bodies + all race-relevant effects fully modelled.

## Approved deferrals
None (full faithful build). Inert-collapse disclosures above are cosmetic, not feature deferrals.

## Depth-ladder + value_play settings investigation (2026-07-31)

**Converged escalation ladder** (3000g/cell, seeds 8008/9009, `--ignore-play-profile --depth`, avg-win-turn lower=better; logs/eval/goblins_depth_overnight.txt + goblins_vladder.txt):
- Heuristic: H1=4.4358, H2=4.4092, H3=4.3988, H4=4.3952, H5=4.3926 (H5 last batch had a ~50-min pathological game — deep-heuristic long tail).
- Value leaf: V5=4.4018, V6=4.3967, V7=4.3947, V8=4.3947 (V8 = full-horizon).
- **Both ladders converge to the same ~4.395 floor.** Heuristic flat above H4 (H4≈H5). Value ties the floor only at V7 (V7=V8); V5/V6 fall short.
- Cross: at MATCHED depth the rollout beats the leaf (H5 4.3926 > V5 4.4018 by 0.009; H3 4.3988 > V5 by 0.003). The leaf reaches deep-heuristic quality only by going deeper (V7≈H4/H5), which it does ~1/30th the cost.
- **Escalation-cap decision: cap at H3** — within 0.004t of the floor; H4 marginal (0.0036), H5 provably adds nothing AND has multi-min pathological games.

**Clean single-thread runtime probe** (50g seed 8008; logs/eval/rt_probe.out): H3=1308ms; V5=254, V6=295, V7=292, V8=288ms. Value ladder is **cost-flat** (V5–V8 all ~290ms) — extra plies add ~nothing because the O(1) leaf caps every branch and games end by ~t5. So V7 is the value-arm sweet spot (floor quality at V8 cost), and V8 is NOT more expensive than V6/V7.

**value_play settings A/B (production path, held-out seeds 2002/3003/4004/5005, 3000g/arm):** baseline `d5/b20/fallback` MEAN=4.3896 (at floor).
- V8-trust (target_depth=8, budget_ms=0 no-budget, no-fallback): quality-NEUTRAL (sanity byte-identical) BUT the unbounded search **HANGS on wide-board goblin games** (huge d8 action trees) — the 20ms/decision budget is **load-bearing** (bounds worst-case latency; the probe's clean 288ms was an easy slice hiding the tail). REJECTED.
- d7/b20 (deeper target, budget PRESERVED): **byte-identical to baseline** (MEAN=4.3897=4.3897 every seed) — inert, the 20ms budget binds before depth 7. REJECTED.
- **DECISION: keep the current adopted `value_play` = target_depth 5 / budget_ms 20 / escalation_cap 5 / fallback ON.** At the quality floor AND robust; no tested variant improves it. Leaf stays adopted (it's what mulligan gen inherits). Scratch variant profiles removed.

## Mulligan profile cost scout (2026-07-31, RUNNING)
Kicked off the R=1 cost scout to understand generation cost (user request):
`./build/Release/mtg-analyze decks/Goblins/Goblins.cod --cards-json src/cards/data/cards.json --gen-mulligan recommend`
→ logs/eval/goblins_mull_recommend.out. Inherits the adopted leaf (rollout depth 5 / budget 20ms from value_play). recommend = one rollout/cell (R=1) → prints GEN-TIME PROJECTION (complete ~Xh / fast ~Yh) + slowest cells; writes a poolable R=1 probe chunk but NO profile (R<10 floor). User to evaluate the projection.
NOTE: `decks/Goblins/Goblins.value.json` is currently UNTRACKED — commit/freeze it before the definitive high-R generation (Rule 0: gen on a frozen commit; the raw sidecar's commit fingerprint gates pooling).

## FINAL play/gen config decision (2026-07-31/08-01) — SUPERSEDES the "keep d5/b20" note above

The "keep d5/b20" conclusion above was only vs the *no-budget* and *tight-budget-d7* variants. A finer
**budget-frontier sweep** (d6 trusted no-fallback × budget {20,40,60,80,100,200}, held-out seeds) found the
budget is the real quality dial and **d6/b40 dominates baseline**:
- Frontier (avg-win-turn, lower=better; baseline d5/b20/fallback = 4.3897 @98s): b20=4.3870@83s, **b40=4.3857@98s**,
  b60=4.3857@115s (dominated), b80=4.3853@124s, b100=4.3847@135s, b200=4.3830@170s. **d6=d7** (7th ply inert).
- **b40 = sweet spot**: −0.0040t at *baseline speed*. VALIDATED on a disjoint seed set (6006/7007/1001/2468):
  −0.0033t, better on 3/4, never worse → not overfit.
- Trusting the leaf (no-fallback) is a PLAY win but does NOT help GEN cost: full-game rollout is the bottleneck,
  not the fallback (d5-trusted 24/s vs 21/s fallback; d6/b40 24/s). K=20/455k hands is the real gen driver
  (legitimate — 1 land bucket + 19 distinct cards, 8 real singletons; NOT a merge bug).

**Gen-config KEY finding (corrects an earlier mistake):** the fast gen path is `mull_gen_depth=3` **WITH the
fallback** — the crossover forces the **heuristic H3 line** (NOT the value-leaf V3). Verified in play: d3+fallback
= 4.3825/4.4025 ≈ H3 (4.3988), nowhere near V3 (4.5775). So d3-gen is fast (~57/s, ~4.4h R=1 floor pass) AND good
rollout quality (H3), not the 0.18t disaster I first feared (that number was V3, the wrong thing).

**ADOPTED (`decks/Goblins/Goblins.value.json`, committed/frozen):**
- Play: `target_depth=6, budget_ms=40`, `value_no_fallback=false` with crossover
  `take_heuristic_at_hdepth=[1,1,1,1,3,9,9,9]` (keep-leaf at committed ≥6, fall back to H only on shallow
  truncation ≤5). Play A/B of this exact config = 4.3870 (beats baseline 4.3897).
- Gen: `mull_gen_depth=3, mull_gen_budget_ms=10` → H3 heuristic rollouts (capped at d3, fallback always fires).
- **Generation plan:** R=1 recommend chunk on the FROZEN commit for a trustworthy `complete`(R40) projection
  (pools into the full run via MTG_KEEP_PRIOR_RAW); launch full R40 if projection ≤4 days. This is the secondary
  machine (can run days). Rough estimate from the ~57/s rate: R=1 ≈ 4.4h, full R40 ≈ 2–4 days.

## Mulligan-gen cost root-cause + branching attribution (2026-08-01, autonomous)

**R=1 recommend chunk COMPLETE** (frozen commit `8a3bbb1`, `logs/eval/goblins_gen_r1_final.out`):
floor pass = **27937 s (7.8 h) @ 32 rollouts/s**, 910034 cells. Built-in projection:
- **COMPLETE (R40) ≈ 310 h ≈ 12.9 days**  — far over the 4-day bar.
- **FAST (R30) ≈ 155 h ≈ 6.5 days**       — also over 4 days.
Both exceed budget → generation needs a real speedup OR another machine / weekend, OR accept >4 d.
The probe chunk (`Goblins.keepmodel.exhaustive.raw.json.probe`) is written and pools into a later run.

**Root cause = plan-enumeration breadth (NOT a stalled-hand / budget issue).** The recommend dump's
top-12 slowest rollouts (17–39 s each vs a ~31 ms median) are all wide-board tutor/payoff hands
(Goblin Matron in 11/12, + Muxus / Aether Vial / Skirk). The cast-subset **odometer** width is
`∏_g(1+|group_g|)·2^|independent|`; it is bounded by neither depth nor the search budget (budget
bounds rollout depth/nodes, not the breadth of plans built at a single node).

**Branching-attribution tool** (already existed: `branchstats`, env `MTG_BRANCH_STATS=1`; wired in
`EnumeratePlans`, reached via `FSLineTail`/`EnumeratePlansWithLand` — the rollout path. NOTE: it does
NOT instrument `TurnSolver::Solve`'s inline odometer twin, i.e. the top-of-turn decision; extend there
if we want that too. Built into `build-instr/mtg` via `-DMTG_PROFILE=ON`, but branchstats is
runtime-gated so a normal build works). Ranked run — goblins d3 heuristic rollout, 300 g, seed 3003
(`MTG_BRANCH_STATS=1 MTG_VALUE_MODEL=0 build-instr/mtg … --depth 3 --ignore-play-profile`):

| driver card | calls | sum_odo | %tot | max_odo |
|---|---:|---:|---:|---:|
| **Goblin Matron** | 693 873 | **143 599 313** | **82 %** | 36 864 |
| Twinshot Sniper | 140 847 | 5 331 771 | 3 % | 768 |
| Siege-Gang Commander | 326 132 | 4 775 314 | 2.7 % | 256 |
| Krenko, Mob Boss | 251 883 | 3 062 166 | 1.7 % | 256 |
| Muxus, Goblin Grandee | 141 461 | 2 341 146 | 1.3 % | 256 |
| (all others) | — | <2 M each | <1 % | ≤512 |

Totals: 175.4 M odometer positions walked → **7.0 M final (deduped) plans** (raw 16.1 M). **Goblin
Matron alone drives 82 % of enumeration work.** Peak single call = 36 864 ≈ ×9 Matron-target options
× 2¹² cast-subset. Worst situations: `groups=5-8 board=7-10/11-15` (wide boards, mid group count).

**Why Matron:** `GoblinsProvider` deliberately uses `GenericProvider`'s **full-candidate**
`TutorCandidates` (every distinct Goblin in library), because narrowing was already REJECTED for play
(the clairvoyant search finds lines through "excluded" targets — DecisionProviders.h:311/324). Each
candidate is a mutually-exclusive `CastFromHand` Action (`tutor_target` set), so the group contributes
`(num_targets+1)` to the odometer product, multiplied against the `2^cast-subset`.

**PROPOSED non-destructive speedup (NOT a cap — factor, don't drop):** Goblin Matron fetches to HAND;
the fetched card is not in the start-of-turn hand the odometer enumerates from, so it is **never cast
the same turn**. Therefore the tutor-target dimension is **provably independent of the same-turn cast
subset** — the `(cast-subset × target)` cross-product re-applies the *identical* same-turn subset
(clone + `ApplyPlanDirect` + eval) once per target, differing only in the card added to hand. Factor
it: enumerate the cast-subsets ONCE and fork the target only at the cheap "add fetched card to hand"
step (sharing the expensive same-turn application). Same final plan set, same rollout values →
non-destructive. Expected ≈ **3.5–4× reduction in total odometer work** (Matron is 82 %; ÷~5–9 on its
calls), concentrated on the exact wide-board tail that dominates the 27937 s floor pass → plausibly
halves-or-better the gen time (FAST R30 6.5 d → ~3 d, COMPLETE R40 12.9 d → ~6–7 d).

**CAVEATS / decision gate (for the user — implement nothing on the frozen play path without approval):**
- This touches the core odometer in BOTH `Solve` and `EnumeratePlans` ("change one, change both") — a
  delicate change that alters play output → must A/B on the regression matrix (frozen depth ladder) and,
  if adopted, re-freeze + **re-run the R=1 estimate** on the new commit before launching R40 (the probe
  chunk's commit fingerprint gates pooling).
- Fallback if we don't take it: `fast` R30 ≈ 6.5 d on this secondary box (over the 4-day bar but
  runnable), or trim scope, or accept.

*Corroboration (d5, 100 g, seed 2002):* same ranking holds at play depth — Goblin Matron = 66 %
of odometer work (320.2 M / 483.3 M), still ~8× the next card; Matron alone 320 M positions → 12 M
kept plans. The 82 % (d3) / 66 % (d5) split confirms the finding is depth-robust, not a d3 artifact.

## Escalation-waste bug + stale value_leaf_table (2026-08-01, found by a second agent, confirmed)

**BUG (perf, confirmed):** in hybrid play (value model + escalation), Goblins runs the deep heuristic
escalation at committed depth 6 and then the crossover's keep-leaf sentinel DISCARDS it — wasted deep
work (and the pathological d6 heuristic games). Chain:
- `escalate = (committed < value_min_depth && !verified)`, `value_min_depth = escalate_below`
  (TurnSolver.cpp ~10758; AIEngine.cpp ~1598).
- `escalate_below = value_trust_depth if set, else target_depth+1`. Goblins `value_trust_depth` is
  **UNSET** → `escalate_below = 7` → escalation fires at committed=6.
- The crossover `take_heuristic_at_hdepth[6]=9` (keep leaf) then throws the escalation away.
- **Root:** `value_trust_depth` is DERIVED from `value_leaf_table`, but the committed table is the EARLY
  **weak-leaf / partial** run (`vdepths=[1..5]`, `hdepths=[1..3]`, `h_conv=4.36`) — **V6–8 and H4–5 are
  MISSING**, so the leaf never reaches convergence in-table → trust un-derived → default 7. `burn` (the
  sibling d6 deck) has a full table and correctly carries `value_trust_depth=6`.

**FIX (confirmed, NO re-measurement — the high-N cells already exist):** the improved-leaf HIGH-N (3000g)
ladder is in `logs/eval/goblins_depth_overnight.txt` (H1–5, V5) + `goblins_vladder.txt` (V6–8): V5=4.4018,
V6=4.3967, V7=V8=4.3947 = H-floor 4.3947. Feed ONLY the high-N logs (NOT mixed with the early 100g H6–7=4.36
noise, which corrupts `h_conv`) to the generator with `--scalar-max-depth 6`:
```
cat logs/eval/goblins_depth_overnight.txt logs/eval/goblins_vladder.txt > /tmp/goblins_highN.txt
python3 scripts/attic/valueleaf_table_to_metadata.py /tmp/goblins_highN.txt --decks goblins --scalar-max-depth 6
```
Dry-run VERIFIED: `trust=6 no_fb=False`. Writing it folds the full V5–8/H1–5 table into the profile AND
derives `value_trust_depth=6` — fixing the escalation waste and repairing the stale evidence in one shot.
value_trust_depth=6 aligns the escalate gate with the crossover keep-leaf-at-6: expected QUALITY-NEUTRAL
(same kept-leaf decision) + FASTER (skips the discarded deep escalation). MUST A/B (d6/b40) before commit;
changes frozen play so re-freeze + re-run R=1 with the other pre-freeze changes. (Note: this is a PLAY-cost
bug — gen runs at mull_gen_depth=3, committed<=3, so it never hits committed=6.)

## Escalation fix — APPLIED + VALIDATED (2026-08-01)

`scripts/attic/valueleaf_table_to_metadata.py /tmp/goblins_highN.txt --decks goblins
--scalar-max-depth 6` (no `--dry-run`) rewrote `Goblins.value.json`:
- `value_leaf_table`: games 400→3000, hdepths [1,2,3]→[1,2,3,4,5], vdepths [1..5]→[5,6,7,8],
  h_conv 4.36→4.3947 (clean floor). V6-8/H4-5 now present. **Folds the depth matrix into the
  play profile** (the user's explicit ask).
- `value_trust_depth: 6` (was UNSET) → `escalate_below = 6` (was `target_depth+1 = 7`).
- crossover regenerated from clean high-N: committed_depths [5,6,7,8], take@[3,4,6,6].

**A/B (500g × 4 held-out seeds 6006/7007/1001/2468, same build/Release, BEFORE vs AFTER profile):**
- Quality: MEAN **4.3935 == 4.3935**, identical *per seed* → byte-identical quality.
- Wall: **82.3s → 63.0s (−23%)** in play at d6/b40.
- Root of the speedup (MTG_HYBRID_STATS, 150g seed 6006): BEFORE `d6 redid 38` of 72 total
  escalations — the deepest, most expensive re-searches, ALL discarded by the keep-leaf sentinel.
  AFTER `d6 redid 0`. The committed<6 (truncated) escalations still fire (genuine fallback);
  only the wasted d6==target ones are gone.

**Scope note:** this speeds up PLAY (search reaches d6). It does NOT materially change mulligan
GEN speed — gen is capped at `mull_gen_depth=3`, below where the d6 waste occurred; the R40 gen
cost is Matron's tutor-branching, a separate lever. Profile change is standalone-validated and
ready to fold into the frozen batch commit.
