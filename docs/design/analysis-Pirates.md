# Analysis ledger — Pirates

Per-deck ledger for the `analyze-deck` workflow (see `.claude/skills/analyze-deck.md`, "The per-deck
ledger"). This file is the durable state a resumed session or a second machine reads to continue;
context is disposable, this is not.

**Deck:** `decks/Pirates/Pirates.cod` — U/R/b Aether Vial Pirates tribal, 60 cards, no sideboard.
**Started:** 2026-09-26. **Branch:** `phase-1-2-deck-analyzer`.

## The list

| n | card | status at Stage 1 |
|---|---|---|
| 4 | Daring Buccaneer | missing |
| 4 | Corsair Captain | missing |
| 2 | Kitesail Larcenist | missing |
| 1 | Malcolm, the Eyes | missing |
| 4 | Staunch Crewmate | missing |
| 4 | Adaptive Automaton | missing |
| 4 | Goblin Tomb Raider | missing |
| 4 | Metallic Mimic | missing |
| 4 | Dire Fleet Captain | missing |
| 2 | Forerunner of the Coalition | missing |
| 2 | Siren Stormtamer | missing |
| 4 | Spirebluff Canal | missing |
| 1 | Blackcleave Cliffs | missing |
| 4 | Aether Vial | implemented |
| 5 | Mountain | implemented |
| 4 | Secluded Courtyard | implemented |
| 4 | Unclaimed Territory | implemented |
| 2 | Fiery Islet | implemented |
| 1 | Lightning Bolt | implemented |

Every "choose a creature type" in the deck (Metallic Mimic, Adaptive Automaton, Secluded Courtyard,
Unclaimed Territory) is **Pirate**.

## Stage 1 — coverage (2026-09-26)

13 missing, 6 `full`. Bracket notes on the implemented cards:

| card | bracket note | classification |
|---|---|---|
| Aether Vial | AI heuristic for charge/activation | not a simplification — the shared root Vial policy (`GenericProvider::WantVialCharge`); disclosed in 6a |
| Secluded Courtyard | type choice simplified to "any creature" | exact here: every coloured-cost creature is a Pirate; the colourless Mimic/Automaton need only generic |
| Unclaimed Territory | "ETB choice and color restriction not modelled" | **note is stale/wrong**: `colored_creature_only` IS set. Exact for this deck (see Stage 2) — note reworded, behaviour unchanged |

## Stage 2 — implementation

Per-card research fanned out to 11 Opus agents (one per card / pair) on 2026-09-26; drafts kept under
`logs/pirates/drafts/` (gitignored scratch). Integration is serial in a scratch worktree off the origin
tip (the main tree held another session's uncommitted discard-gate WIP).

| card | tier | mechanism (new params in **bold**) |
|---|---|---|
| Spirebluff Canal, Blackcleave Cliffs | 1 | existing fastland (`fastland_max_other_lands`) |
| Siren Stormtamer | 1 | vanilla 1/1 flier; activation unmodelled (PROVISIONAL, see below) |
| Staunch Crewmate | 2 | existing `etb_dig_*`; dig filter now matched by `CardMatchesTypeName` so `"Artifact"` works |
| Dire Fleet Captain | 2 | Piledriver attack pump + **`attack_pump_tough_per_other_matching`** |
| Corsair Captain | 2 | `lord_effect` + **`etb_creates_treasures`** |
| Goblin Tomb Raider | 2 | **`static_artifact_threshold/power/tough/haste`** conditional static + conditional haste |
| Forerunner of the Coalition | 2 | Giant Harbinger tutor-to-top + **`own_creature_enters_opp_life_loss`** |
| Malcolm, the Eyes | 2 | **`nth_spell_trigger_n` / `nth_spell_investigate`** (Clue) |
| Metallic Mimic, Adaptive Automaton | 3 | **`chosen_type_added_to_self`**, **`other_chosen_subtype_enters_counters`**, **`lord_affects_chosen_subtype`**: the chosen type (DominantCreatureSubtypeId = Pirate) is appended to the BATTLEFIELD permanent's subtypes only |
| Daring Buccaneer | 2 | **`reveal_or_pay_subtype/cost`** in EffectiveSpellCost + an order-aware per-subset surcharge in the enumerator |
| Kitesail Larcenist | 3 | **`etb_treasurify_each_player`**: in-place rewrite to a Treasure, searched own-side target, new `treasurify` viewer decision |

### Pre-existing bugs found in OTHER decks during research (verified 2026-09-26)

1. **Keyword mask overflow** — `Keyword` has 35 values, `Card::m_keyword_mask` was `uint32_t` with
   `1u << k`: Persist/Evoke/Convoke were UB and on x86 aliased Haste/Flying/Trample (probe: adding
   Persist stores mask `0x1`). **Kitchen Finks and Murderous Redcap (Melira Pod) had phantom haste.**
   Pirates needs `Ward`, so the fix is on the critical path. Own commit; Melira Pod GT re-accepted.
2. **Felidar Guardian decline divergence** — `AIEngine` only stores `chosen_x` on the stack entry when
   `> 0`, so a searched DECLINE (0) resolves in the executor as `-1` -> provider `FlickerTarget`, while
   the rollout declines. Executor/rollout divergence (Melira Pod).
3. **Oko's Elk keeps its old definition** — `elk_transform` renames to "Elk" but never clears the cached
   `Card::m_def`, so `LookupCached` still returns e.g. Birds of Paradise's definition (FiveColour).

## Approved deferrals

(none yet — every deferral below is PROVISIONAL until the user signs it off)

PROVISIONAL:
- **Siren Stormtamer** — "{U}, Sacrifice: counter target spell or ability that targets you or a creature
  you control" unmodelled: never legally activatable (the passive opponent never targets), and nothing
  in the 60 has a death payoff.
- **Kitesail Larcenist** — the "for as long as it remains" revert (it cannot leave the battlefield in
  this list); opponent-side target auto-declined in autonomous play (spawns never block, nothing reads
  them; human can still pick); rename to "Treasure Token" (rules keep the name; inert here); flying/ward inert.
- **Corsair Captain** — the global `MTG_PAYSAC_FRESH_HOLD` doctrine banks a Treasure made this turn;
  real Magic can spend it at once (e.g. T3 Captain -> crack for a 1-drop). A NARROWING — under review.
- **Chosen creature type** (Mimic / Automaton) not surfaced in the viewer: deck-constant Pirate weakly
  dominates every alternative (every other creature is a Pirate).
- **Daring Buccaneer** reveal-vs-pay auto-resolves to reveal (paying {2} when a reveal is possible is
  strictly dominated).

## Discard policy

**Status: AUTHORED, implemented default-on, PENDING USER REVIEW** (adoption is a user review — the same
gate as cast order; nothing here records a confirmation). Full proposal:
`docs/design/pirates-discard-policy-proposal.md`. Code: `PiratesProvider::CleanupDiscardCandidates`
behind `MTG_PIRATES_BUCKET_DISCARD` (default ON, `=0` restores the generic max-MV ranking).

* Shape: simple aggro → **two buckets**, MANA (lands + a Vial sub-role) and THREATS (catch-all).
* MANA / LANDS: reach **4 sources** net of board; the first two land slots before the threat floor,
  the rest after. Colour coverage first (creature-only lands cover creature pips, never Bolt's),
  then breadth; a Fiery Islet is the last surplus land shed.
* MANA / VIAL: **1** only while no Vial is on board and the board has ≤ 2 lands; else surplus.
* THREATS: hard floor **3**; overflow shed FAR-first (distance ≥ 2 incl. a missing colour; a board Vial
  or Buccaneer's reveal erases it), then by a param-derived value: lords 100–104 > Mimic 92 > Dire
  Fleet Captain 86 > Forerunner 82 > Malcolm 75 / Larcenist 72 > Crewmate 62 > Bolt 56 > 1-drops ~38–46.
* Shed order: dead legend → surplus lands → surplus Vials → overflow threats → quota tail (every hand
  card named; no max-MV fall-through).
* Gate: the main tree's `scripts/discard_policy_audit.py` classifier (run against this worktree via a
  symlinked scratch root under `logs/daudit/`) reports **Pirates → OK** (not MISSING / PATCH-ONLY /
  INHERITED).
* Doubts for the user (§8 of the proposal): land target 4 vs 3; the Vial rule; the 1-drop order; Bolt's
  slot; floor-before-late-lands; fastlands not demoted; Treasures not counted.
* Evidence still owed: `--discard-analysis` + non-inferiority vs `=0`, after the Stage 4 profile.

## Open questions for the user

(collected here; never blocking — defaults taken are stated)

1. Sign off the PROVISIONAL deferrals above.
2. **Cast order (USER review — NOTHING ADOPTED).** `PiratesProvider` does NOT override
   `CastOrderRank`; the deck runs the ROOT default (`GenericProvider::CastOrderRank`: creatures 10,
   other non-creatures 20, mana rocks 5, accelerant tiers 15/16/18) and the search decides every
   ordering the canonical line leaves open. Proposals, each to be measured behind a default-OFF
   `MTG_PIRATES_*` lever only after sign-off:
   * **2a — Metallic Mimic before Malcolm (and before every other Pirate).** Mimic's +1/+1 counter
     is permanent and accrues to every Pirate cast after it; Malcolm's Clue fires on the SECOND spell
     of the turn regardless of which spells, so Malcolm need only not be the turn's first-and-only
     spell. Proposed: Mimic rank 8 (< creatures 10) whenever another Pirate is cast the same turn;
     Malcolm stays at 10. No counter-case found: Malcolm's haste attack happens in combat after both
     have resolved, and cast first he would himself receive Mimic's counter only if Mimic preceded him.
   * **2b — Daring Buccaneer FIRST among Pirate casts.** Its cost is `{R}` only while another Pirate
     CARD is in hand to reveal; casting the other Pirates first can strand it at `{2}{R}`. The
     enumerator already prices this per subset (chunk 2's order-aware surcharge), so the rank only
     matters for the canonical line. Proposed: Buccaneer rank 9 when a reveal is available.
   * **2c — Forerunner of the Coalition FIRST among the non-Buccaneer Pirates.** Its drain is
     "whenever ANOTHER Pirate you control enters", so every Pirate cast before it drains nothing.
     Tension with 2a (Mimic first grows the Forerunner too) and 2b. Proposed total: Buccaneer 7 (cheap,
     reveal-dependent) → Mimic 8 → Forerunner 9 → other creatures 10. If the mana only allows two,
     the search decides which.
   * **2d — Vial-put pick.** Which creature a Vial at N counters puts in is today the root Vial policy
     plus the search. Proposal: prefer Forerunner / a lord at N=3 over Kitesail Larcenist, and Mimic
     at N=2 over Crewmate/Malcolm/Dire Fleet Captain (same reasoning as 2a/2c). This is a *put-order*
     heuristic, not a charge policy — the charge policy (`WantVialCharge`) stays root.
   Default taken: root order, pending review.
3. OK to classify the new params in `audit_viewer_decisions.py` INERT_PARAMS (script self-guard)?
4. Corsair's same-turn Treasure: lift the fresh-hold for non-trick Treasures (A/B-measured)? Default: measure.

## RESUME STATE (2026-09-26, pre-compaction)

* **Worktree:** `/tmp/pirates-wt`, branch `pirates-analysis` (based on origin tip `c29b9a61`). The main
  tree `/workspaces/MagicDeckTester` holds ANOTHER session's uncommitted discard-gate WIP (cards.json,
  DecisionProviders.*, verify_deck.py, CLAUDE.md, discard_policy_audit.py, 15 proposal docs) — never
  commit or revert it from here. This ledger and `decks/Pirates/Pirates.cod` are still UNCOMMITTED in
  the worktree. Drafts: `/workspaces/MagicDeckTester/logs/pirates/drafts/*.md`.
* **Committed locally (not pushed):** `52bb7af9` keyword mask -> 64 bit (+Ward); `23369f3b` chunk 1
  (11 cards + chosen-type infra; 179 unit tests pass; coverage: only Buccaneer + Larcenist missing).
* **In flight:** integrator chunk 2 (background agent) — commits, in order: Felidar decline fix,
  Oko Elk m_def fix (+ Card rename helper), Daring Buccaneer (EffectiveSpellCost + per-subset
  surcharge + SubsetPayableSequential bookkeeping), Kitesail Larcenist (+ `treasurify` viewer decision).
* **Next — chunk 3:** `PiratesProvider : DeckProvider` (NOT VialProvider — Vial policy is root default;
  VialProvider only adds Knights-gated cast order); route it ABOVE `anti` in DetectDecisionProvider
  (Forerunner's tutor_to_top trips `anti`) with a signature OR-ed from several Pirates-only params;
  `Certificate()` answer; `TutorSearchWidth` >= 9 (9 distinct Pirate names; default 6);
  bucketed discard policy per `/workspaces/MagicDeckTester/docs/design/discard-bucket-authoring-brief.md`
  behind `MTG_PIRATES_BUCKET_DISCARD` + `docs/design/pirates-discard-policy-proposal.md` (USER review);
  cast-order proposals (Malcolm/Mimic, Buccaneer first, Forerunner first) are USER-reviewed — propose only.
  Then `python3 scripts/provider_audit.py --check`.
* **BOX HOLD:** load avg ~24/24 from a sibling container (likely the Fungus generation; nothing
  visible in this container). One-batch-at-a-time rule => NO smoke/regression/sweeps until the box is
  quiet (a background watcher loop was checking `/proc/loadavg` < 8 for 10 min). Builds + unit tests OK.
* **Then:** smoke tier (byte-identity except Melira Pod [keyword, Felidar] / FiveColour [Elk] — each
  changed game needs a verdict; accept per regression-testing skill), Stage 4 profile
  (`analyze_deck.py --no-rebuild`), Stage 5 (`verify_deck.py`, nonconv/fd-diverge, d0/3/5 sweep,
  leaf_tiebreak_check, claude-play sweep ~15-20 Opus/Sonnet agents, viewer audit), fresh-hold A/B
  (`MTG_PAYSAC_FRESH_HOLD=0`) to bound Corsair's same-turn Treasure narrowing, add Pirates to the
  regression suite, push via this worktree (`git pull --rebase` first; rebuild + smoke after rebase),
  watch CI (Windows).

## Chunk 3 landed (2026-09-26) — provider, routing, certificate, tutor width, discard policy

Integrated serially in `/tmp/pirates-wt` (branch `pirates-analysis`, local, NOT pushed).

| commit | what |
|---|---|
| `097e913f` | `PiratesProvider : DeckProvider` — routing above `anti`, `Certificate()` NotAssessed, `TutorSearchWidth` 9; routing/width unit tests |
| `d5d6220e` | authored bucketed cleanup-discard policy behind `MTG_PIRATES_BUCKET_DISCARD` (default ON) + `docs/design/pirates-discard-policy-proposal.md` + 7 discard unit tests |
| (this commit) | ledger: `## Discard policy`, cast-order proposals 2a–2d, this section |

**Routing.** Before: Pirates → **AntiLifegain** (Forerunner's `tutor_to_top`; also Mill and Unpredictable
Cyclone → AntiLifegain). Signature = `reveal_or_pay_subtype` | `etb_treasurify_each_player` |
`attack_pump_tough_per_other_matching` | `nth_spell_investigate` | `own_creature_enters_opp_life_loss`
(five cards; each param carried by no other card in `cards.json`). Deliberately excluded: `tutor_to_top`,
the colourless Mimic/Automaton chosen-type params, `etb_creates_treasures`, `static_artifact_*`.
Verification:
* `scripts/provider_audit.py --check` over all 25 profiled decks: **identical** to the pre-change run
  (only the "not assessed" certificate count moves 22 → 23 for the new class). Saved:
  `logs/pirates_chunk3/provider_audit_{before,after}.txt`.
* `--batch` probe of the three unprofiled lists: Pirates → **Pirates**; Mill, Unpredictable Cyclone →
  AntiLifegain (unchanged).
* Unit test pins the provider of EVERY folder in `decks/` (and fails if a new folder appears without an
  entry), plus the signature's survival of any single-card cut.
* Byte-identical play for every other deck holds **by construction** (the signature params exist only
  on Pirates cards; nothing else changed for another deck). Smoke NOT run (box hold) — see PROVISIONAL.

**Why DeckProvider and not the AntiLifegain it rode.** The skill's "derive from what the deck actually
routes to today" protects play-neutrality for an already-measured deck; Pirates was never measured on
AntiLifegain, which was a misroute, and deriving from it would import `TutorSearchWidth` 2 (hiding 7 of
9 Forerunner targets) and another deck's buckets. PROVISIONAL-by-default only in that the user has not
seen it; the RESUME STATE already prescribed DeckProvider.

**Tutor width.** 9 distinct Pirate names in the library (Mimic/Automaton are Pirates only on the
battlefield); generic candidates are in shuffle order, base width 6 → 3 names unreachable per game. Width
only feeds the TurnSolver tutor axis (`TutorAxisWidth`, additive, one rollout per extra target) and
GoblinsProvider internals; `MTG_TUTOR_WIDTH` still overrides.

**Results.**
* `./build.sh` clean; `build/Release/mtg-test` **207/207 pass** (10 new Pirates-provider cases).
* `python3 scripts/audit_viewer_decisions.py decks/Pirates/Pirates.cod --no-sweep`: exit 0; expected
  decisions `dig, target, treasurify, vial_charge`; oracle cross-check clean.
* `analyze_deck.py --coverage-only`: **19/19 full**.
* Discard gate (main tree's `discard_policy_audit.py` classifier run against this tree): **Pirates → OK**.
* Sanity: `build/Release/mtg decks/Pirates/Pirates.cod --games 10 --threads 2 --seed 4242` → avg 5.10,
  per-game win turns `-,4,4,5,5,4,5,5,5,5` — **identical** to the pre-chunk-3 run of the same seed
  (`logs/pirates_sanity`). Game 0 is unwon in both: two Mountains, no blue/black, and the Aether Vial
  **never charges** — because there is no `.profile.json` yet, so `vial_target_mv` = 0 (the known
  profile-less behaviour, `src/analyzer/main.cpp` note). NOT a play bug; it disappears with the Stage 4
  profile. The three cleanup sheds in that game match the policy (FAR Forerunner/Larcenist shed first).

**PROVISIONAL (awaiting the user):**
* The discard policy itself (default ON, user review pending) and its seven doubts.
* Cast-order proposals 2a–2d (nothing adopted; root order in force).
* Smoke / regression byte-identity NOT run — box held by a sibling container (one-batch rule). Owed
  before push, together with the Melira Pod / FiveColour verdicts from chunks 1–2.

**Found in another deck (not fixed — out of scope):** `GiantsProvider` does not override
`TutorSearchWidth`, and Giants holds **7** distinct Giant names for Giant Harbinger (Inferno Titan,
Sunrise Sovereign, Giant Harbinger, Borderland Behemoth, Hamletback Goliath, Surtland Flinger, Tectonic
Giant) against the base width 6 — the same shuffle-order coverage hole fixed here for Pirates, one name
unreachable per game. A width-7 override would widen (not narrow); it changes Giants' play, so it needs
its own measurement and GT verdict.

**Next:** unchanged from RESUME STATE after chunk 3 — smoke (when the box is quiet), Stage 4 profile
(`analyze_deck.py --no-rebuild`), then `--discard-analysis`, Stage 5 (`verify_deck.py` incl. the
`discard_policy` gate once the main tree's gate lands), claude-play sweep, fresh-hold A/B, suite entry,
rebase + push + CI watch.

## Sweep fixes (2026-09-26) — Stage 5d claude-play findings A–G

Integrated in `/tmp/pirates-wt` (branch `pirates-analysis`, local, NOT pushed). Sweep: 20 Opus games,
base 777000, findings in `logs/pirates_sweep/results.md`. `build/Release/mtg-test` **216/216 pass**
(9 new cases across `test_pirates.cpp` / `test_pirates_provider.cpp`). No smoke/regression run by the
integrator (the orchestrator runs the suite).

| finding | verdict | commit |
|---|---|---|
| **G** SLOW-GAME repro printed `--game-index 0` under `--game-index N` | real (output only). `GoldFishRunner`'s SLOW-GAME line and main.cpp's "Unwon games" list printed the LOCAL index; the replay needs the GLOBAL one (spawn pattern `PATTERNS[gi % 10]`). Now `base_game_index + gi`, matching the batch twin. Log FILE names keep the local index on purpose (tools key them `<base_seed>_game_<local gi>`). | `c241c49c` |
| **C** Fiery Islet plans 6x in the human menu | real, human-menu only. The 5 twins are `AppendBreakpointVariants`' bp_choice variants (`MTG_BP_DIG_SELF_SOURCE`: a plan whose own land drop is a sac-draw land is fanned out, W=2 × 2 indices + the empty arm). Under human play NO breakpoint continuation is ever re-solved (`!s_human_play` everywhere), so every bp variant realises its base plan — for every deck and site. Fix: hidden from the capped menu via the existing `hide_bundle` (indices stay real; uncapped protocol checker and chosen-extra path unchanged). **Search side: by design, not changed** — the self-source fan-out is deliberately state-independent (its note accepts duplicate variants when the dig is unaffordable); decks with Fiery Islet: Pirates, treasure_hunt. Measured Pirates gi11 d5/b200 with `MTG_BP_DIG_SELF_SOURCE=0` vs `1`: identical game, no measurable wall difference. | `ba4da21f` |
| **D** Vial-put Crewmate digs before Mimic's as-enters counter | real (rules order). Both Vial twins (`AIEngine` deploy_via_vial / `TurnSolver` apply_vial) ran `PerformEtbDig` before `FireEtbWatchers`/`FireOwnEtbTriggers`; every cast path runs the cascade first (CR 614 replacement precedes the CR 603.6a trigger). Both now dig after the cascade, addressing the entrant by slot (the cascade can push tokens). | `dc33e254` |
| **A** Forerunner "you may search" had no decline plan in human play | real. The search's decline is the index axis (`kTutorDeclineChoice`, autonomous-only); a PERMANENT's ETB tutor has no resolution frame, so its named menu variants were the human's only say. Human play now appends a `(decline search)` variant (`kTutorDeclineTarget`), resolved as a decline by the one shared `PerformTutor` (executor and rollout lockstep for free). Tutor spells already re-ask with -1; the Vial/put route already asks `tutor_etb` with -1 = decline (checked, no change). Also applies to Giant Harbinger (Giants human menu gains one variant). DECISIONS.md updated. | `b54557b6` |
| **E** main-1 drain to 0, game continued into combat (0 → -17) | **harmless, documented** — not changed. `GameEngine` checks the win after combat and at end of turn, so the win turn is the same turn; every search site tests `OpponentHasLost` right after `ApplyPlanDirect`, so a main-phase drain kill scores as a this-turn win (main 2 likewise; the engine never Vials at instant speed on the opponent's turn). An SBA check after main 1 would only truncate post-lethal combat in the log and move the play digest of every deck with a main-phase kill for no win-turn change. Unit test pins the search half. | `d6cfbb2a` |
| **B** Vial puts always resolve before casts | real expressiveness gap. New `Plan::vial_after_casts`: `AppendVialOrderVariants` adds one casts-first clone per base plan where `TurnSolver::VialOrderMatters` (a cast carrying `other_chosen_subtype_enters_counters` / `own_creature_enters_opp_life_loss` / `reveal_or_pay_cost` alongside a Vial put), on all three enumeration exits. Both orders are scored; the SEARCH picks (no order dominates: Vial-put Forerunner + cast Mimic trades a drain for a counter). Lockstep: rollout defers the top-level `apply_vial` loop to right after its graveyard casts (arm-once, continuations unaffected); executor defers `deploy_via_vial` to right after its graveyard-cast loop. Menu summaries now list entries in RESOLUTION order where the predicate holds; plan JSON `vial_after_casts`; CheckLine `Aether Vial timing` sub (viewer regex updated). `MTG_VIAL_ORDER_AXIS=0` disables. Verified: gi17 variant → Siren Stormtamer enters with Mimic's +1/+1 (executor path); gi5/7/13/17 d5/b200 benches still T4 with `MTG_FD_ORACLE=1` and no `[fd-diverge]`. | `9ede989b` |
| **F / gi14** Crewmate dig took Aether Vial over a 2nd Goblin Tomb Raider | real expressiveness gap (budget-invariant T5 at b200/b2000/b20000). The dig axis scores the first 3 of the provider ranking; the base ranking is every legal match in look order, so two Aether Vial COPIES filled two slots and the Raider (4th) was unreachable. `PiratesProvider::EtbDigCandidates` folds same-name copies (exact: the rest go to the bottom in random order) and new provider-owned `EtbDigSearchWidth` = 4 (the look count). gi14 hand-off now wins **T4** at b200. | `a68a97ac` |
| **F / gi11** Crewmate dig → same-turn Daring Buccaneer | real expressiveness gap, **DEFERRED** (`docs/design/etb-dig-same-turn-cast.md`). An ETB dig never opens a breakpoint at searched depths: the rollout arming is a recorded rejection (`MTG_ACQ_DIG`, 2026-08-19), and the general put-in-hand rule stands down because `ParamKeyedDrawClass` claims `etb_dig_count`. Hand-off T5 at b200 and b20000 (Claude T4). Fix is engine-wide, moves Knights, needs its own A/B. | (doc, this commit) |

**Which other decks' play can move (for the suite check):**
* **None by construction** for G, C, A (human-play / output only), D (only Vial-put `etb_dig` creatures are Staunch Crewmate and Acclaimed Contender; no Knights card carries an enter watcher or own-ETB trigger, so the reordered cascade is a no-op there), B (gate params exist only on Pirates cards; `VialOrderMatters` short-circuits on "no ActivateVial"), F/gi14 (base ranking and width untouched). E is a test only.
* **Pirates** play changes (B, D, F/gi14) — Pirates GT is being created now, so there is no baseline to move.
* Human-play menus change for Pirates (A, B, C), Giants (A: Giant Harbinger decline), and every deck showing bp_choice twins (C: fewer menu entries, indices unchanged).

**PROVISIONAL / residual risks:**
* B's pricing gap (documented at `AppendVialOrderVariants`): a subset affordable ONLY casts-first (a Buccaneer that must reveal the Pirate a tight-mana turn also Vial-puts) is still not enumerated; the clone only re-orders subsets the puts-first pricing admitted.
* B's lockstep scope excludes plans that open a breakpoint (the deferred puts are not threaded through continuations); such a plan keeps puts-first only.
* gi13's autonomous bench still ends with a counterless Buccaneer: the casts-first variant IS enumerated, but both orders win T4 and the win-turn tie keeps the base (puts-first) plan. The search's metric does not price the +1/+1; not a defect.
* Found, NOT fixed (pre-existing, other decks): the ROLLOUT's whole-turn batch prepay (`BatchPrepayMainCasts`) runs BEFORE its Vial loop (inside `apply_plan_actions`) while the EXECUTOR deploys Vial puts first and prepays after. Payment-quality only (casts reprice live), but it means the two worlds prepay from different boards on Vial turns (a Vial-put cost reducer such as Goblin Warchief, or a Buccaneer reveal). Worth an fd-diverge look on Goblins/Knights.
* F/gi14's name-dedup is Pirates-only; Acclaimed Contender (Knights) keeps copy-occupied slots under the base ranking — the same reach hole, left for a Knights-measured change.
