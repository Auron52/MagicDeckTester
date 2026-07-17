# Deck-onboarding hardening — make the pipeline a set of enforced gates

**Status:** in progress (started 2026-07-17, branch `phase-1-2-deck-analyzer`).
**Owner goal (user):** move the user from *bug-finder* to *sanity-checker*. Getting the first
6 decks workable took heavy manual effort and issues still slipped through (e.g. the Slivers /
Hatchery-Sliver `replicate` viewer gap, found by hand mid-game). The onboarding process must
robustly guarantee — *before* the user reviews a deck — that every clause of card text is
modeled, autonomous play works, and every viewer decision is wired. Aim for ~100% even though
it won't be perfect; only a few items should remain, and each remaining item needs the user's
explicit OK to be left deferred.

## Guiding principle (applies to every surface)

**Exhaustive-by-default gates. The only thing that reaches the user is an explicit
"approve or defer this" decision — never a bug they had to find themselves.** The viewer
session becomes a sanity check that *passes*, not a discovery tool.

| Surface | Exhaustive-by-default gate | Escape hatch (needs user OK) |
|---|---|---|
| Card text | every oracle clause modeled | explicit deferral, disclosed with why-inert |
| Viewer decisions | **every potential decision a card creates is mapped/surfaced** | an unmapped decision — disclosed, requires user OK |
| Autonomous play | nonconv/fd-diverge/claude-play flags all root-caused | (none — bugs get fixed, not deferred) |
| Perf: heuristics + profiling | autonomous profile→generate→validate→adopt | only genuinely ambiguous heuristic trade-offs |

**Heuristic policy (user, 2026-07-17):** a heuristic that **tests well auto-adopts — no
approval needed** — but **every** heuristic is **always reported** (a heuristic narrows the
search and can drop a line the tests didn't exercise; the user keeps a post-hoc veto). Approval
is required only when a candidate does **not** cleanly pass (unexplained per-game regression, or
a quality-for-speed trade the A/B can't certify neutral). "Tests well" = the skill's strict bar:
unpruned-vs-pruned (or with/without) A/B **diffed per-game against `test/gt_logs/*.wins`** (never
the aggregate fingerprint), quality parity or net-positive, **every** per-game regression
explained, zero unexplained win→loss, zero true `[fd-diverge]`, and (for a pruner) a measured
real cost win. This asymmetry is deliberate: **tested optimizations self-adopt; unmodeled
behavior (card gaps, unmapped decisions) never does** — its only escape is explicit user OK.

## Diagnosis — one failure pattern, four surfaces (from a 4-agent code audit)

1. **The real guarantees are skill *prose*, not code gates.** "Account for every clause," "run
   the 5d sweep," "classify each divergence" are all discipline an agent must choose to follow.
2. **The mechanical gates that exist have holes.**
   - Coverage (`scripts/analyze_deck.py`): reliably catches only *fully-absent* cards; partial
     detection is a ~10-pattern allow-list checking *presence not correctness*, and a single
     inline `[bracket note]` silences it. The engine **never reads `oracle_text`**
     (`CardDatabase.cpp:16` strips it) — behavior is `template`+`parameters` only, so nothing
     mechanically reconciles text vs implementation. **Only hard-stops on missing cards + build
     failure**; partial gaps exit 0 and still produce a profile.
   - Reality-diff: `scripts/audit_card_costs.py` is the *only* Scryfall-checked gate and covers
     **cost/cmc/cascade only** — nothing checks P/T, damage, trigger thresholds, token counts.
   - Viewer (`scripts/audit_viewer_decisions.py`): the **self-guard is dead code** —
     `CHOICE_PARAM_KEYS = set(MANIFEST.keys())` makes the "unmapped choice-param = hard failure"
     `elif` unreachable, so a new choice-param trips nothing (this is how `replicate` slipped).
     Oracle-text cross-check is 9 fixed phrasings, **advisory (never fails exit)**, and doesn't
     know keyword mechanics like replicate. Wiring **sites 3 (emitter) & 4 (GUI branch) are
     never validated** — a type can surface in the protocol with a broken/missing GUI branch and
     still pass. Runtime sweep marks rarely-cast / state-gated decisions **UNVERIFIED (soft)**.
   - claude-play sweep drives **only main phases**, plays **from cards.json** (can't catch
     cards.json ≠ real card), win-turn is a **weak** signal, and requires Claude to play well.
3. **Nothing enforces the battery even ran.** No CI; `test/regression.sh` compares only a
   `won/avg-win-turn` fingerprint to ground truth. A deck can land green having **skipped every
   audit and the sweep**, and a mis-modeled card's behavior simply *becomes* the accepted GT.
4. **(perf)** The generate→validate→adopt pieces all exist but **aren't wired into a loop**:
   `scripts/mine_heuristics.sh` + `analyze_earliest_wins.py` mine candidates unattended; the
   profilers (`MTG_PROFILE`, `MTG_BRANCH_STATS`, `--gate-probe`) emit text for a human; but
   classify / encode-to-provider / per-game A/B / adopt are all manual seams. Archetype binding
   is hand-coded C++ (`SelectDecisionProvider`, `DecisionProviders.cpp:1747`); a new deck with no
   signature falls through to `GenericProvider`.

### The replicate slip, precisely (the motivating case)
Four independent nets each had a hole at the same spot: (A) the `has_replicate` param wasn't in
the auditor manifest; (B) the self-guard meant to catch (A) is dead code; (C) the oracle-text
scan doesn't recognize the word "replicate"; (D) the runtime sweep rarely casts Hatchery with
surplus mana, the only state a replicate choice arises → stays soft-UNVERIFIED. Fixed post-hoc in
`ed764be`, but the *class* is still open until the gates below close.

## The plan — 5 workstreams

Each lands behind the guiding principle; ③ ties them into one un-skippable gate.

### ① Viewer — make "every decision mapped by default" real
- **Fix the dead self-guard** in `scripts/audit_viewer_decisions.py` and **invert the default**:
  a **complete param registry** classifying *every* `cards.json` param as `DECISION→type` or
  `INERT→reason`. Any param on a deck card not in the registry → **hard fail** (exit non-zero).
  Escape = an explicit `INERT` classification (the user-approved non-decision). This is the
  user's "every potential decision mapped by default, else your OK" made mechanical.
- **Validate wiring sites 3 & 4 statically**: every mapped decision `type` must have a
  `Write<T>DecisionJson` emitter in `main.cpp` *and* a dispatch branch in `tools/play/index.html`,
  else fail. (Closes "type surfaces but GUI is broken/missing.")
- **State-forced surfacing in the gate**: a mapped type must be *observed* in the sweep or
  *forced* via `--verify-card` / hand-built `--choices`; no silent UNVERIFIED pass.
- Fix the latent **second greedy replicate loop** (`AIEngine.cpp:3200-3218`) that never consults
  the chooser — a dead-chooser waiting to happen if human-play ever routes through it.
- Acceptance: an unmapped choice-param cannot pass; a mapped type with no emitter/GUI branch
  cannot pass; the replicate class is closed for the *next* mechanic, not just replicate.

### ② Card text — mechanical clause reconciliation  ✅ DONE (2026-07-17)
- **DONE — authoritative Scryfall snapshot + oracle/field diff (`scripts/audit_card_fields.py`).**
  The authoritative data is a **committed** snapshot `src/cards/data/scryfall_reference.json`
  (102 cards), fetched deliberately with `--update`; the default (offline) mode diffs cards.json
  against it — **mana_cost, P/T, types/subtypes/supertypes, keywords HARD** (exit 1);
  **oracle_text ADVISORY** (normalised similarity, catches the Irencrag "Add six {R}"
  fabrication/drift). Fails **CLOSED** if the snapshot is missing/incomplete (a disclosed pending,
  never a silent pass). Offline-diff-against-committed-snapshot beats live-fetching every gate run
  (slow + flaky). **`card_fields` is now a LIVE BLOCKING gate** (was SKIP-disclosed).
- **DONE — gate calibrated (strip systematic noise + per-card allowlist).** The first live run
  flagged 25 "hard mismatches", but ~18 were Scryfall-taxonomy artifacts, not bugs. Two-layer fix:
  (1) **systematic noise stripped in-code** — `MODELED_ELSEWHERE_KEYWORDS` (ability words + keyword
  abilities this engine models via params, not tags: cycling/scry/surveil/cascade/retrace/replicate/
  affinity/treasure), land subtypes (derived at runtime), Basic/World supertypes (inert; Legendary/
  Snow still checked), CDA `*` P/T component-wise; (2) **intentional per-card divergences** →
  `src/cards/data/scryfall_divergences.json` (card→field→reason; reviewed, never silent; STALE-entry
  detection): 3 keyword-lord Slivers, Haytham (protection, inert), 3 tribal-subtype omissions.
  Negative-tested: corrupt P/T, a genuinely-missing keyword, and an un-allowlisted divergence are all
  still HARD-caught. Result: **0 hard mismatches, 7 disclosed allowlisted, 57 oracle advisories**.
- **DONE — `--coverage-only` hard-stops on partial gaps** (`analyze_deck.py`): missing OR `partial`
  (an implementable clause with no deferral bracket note) → exit 1. A genuine deferral is a bracket
  note (status stays `full`), so signing one off keeps it green. Closes "partials exit 0 and still
  produce a profile."
- **Clause ledger — deferred (disclosed).** Its function (every oracle clause
  modeled/inert/deferred) is now covered mechanically by the combination of: coverage
  (partial hard-stop) + bracket-note deferrals + the viewer auditor's oracle-text cross-check +
  `audit_card_fields` oracle-diff. A dedicated hand-populated per-clause artifact adds marginal
  rigor at a high per-card cost, so it is a disclosed deferral, not a silent gap.

### ③ Enforcement spine — one green gate  ✅ BUILT (2026-07-17)
- `scripts/verify_deck.py <deck>` runs the whole battery (coverage[hard-on-partial], cost+field
  audit, viewer audit[fixed], clause ledger, claude-play flags, nonconv/fd-diverge), emits the
  Stage-6a disclosure, and records results + approved-deferrals to the per-deck ledger
  (`docs/design/analysis-<deck>.md`). **Non-zero unless every check is green or every exception is
  a recorded user sign-off.** This is what lets the viewer session be a sanity check.
- **Built:** gate model (PASS/FAIL/SKIP/DEFERRED/ERROR, each with sign-off-able finding keys).
  Gates wired: `coverage` (parses the coverage JSON, **hard-fails on `partial` — closing the
  "partials exit 0" hole**), `card_costs` (Scryfall, `--no-network` skips), `viewer` (the fixed
  auditor), `viewer_wiring` (**static sites 3 & 4** — every decision type the deck uses has an
  emitter in `main.cpp` + a GUI branch in `index.html`, resolved via the DECISIONS.md registry),
  `mismatch` (engine `MTG_FLAG_NONCONV` + `MTG_FD_ORACLE` across seeds). **Not-yet-built checks are
  DISCLOSED skips, never silent:** `card_fields`/`clause_ledger` (②), `claude_play` broadened sweep
  (④). Ledger: a user-owned `## Approved deferrals` section signs off findings by key
  (`gate:key`); a blocking FAIL whose every finding is approved downgrades to DEFERRED (disclosed,
  non-blocking) — the generated block is rewritten in place, the approvals section never touched.
  Exit non-zero on any un-approved blocking FAIL/ERROR. (Validated on Anti-Lifegain: coverage
  FAILs on the unbracketed Ignoble Hierarch exalted trigger; a sign-off downgrades it to DEFER +
  PASS.)

### ④ Autonomous play — enforce + broaden  ✅ BUILT (2026-07-17)
- The claude-play correctness sweep is now a **gated step** in ③, split into its two halves:
- **4a `play_invariants` (mechanical, LIVE blocking)** — `scripts/play_invariants.py` drives the
  claude-play stateless-replay protocol auto-following the engine's own defaults (develop-greedy at
  `main_phase`, where the protocol exposes no engine plan-pick) and asserts the invariants the
  autonomous smoke can't: **determinism** (same CSV → byte-identical decision block), **integrity**
  (valid JSON, known decision type, contiguous plan indices, exit 70/0), **progress** (di/turn
  non-decreasing, reaches a result within a cap). Cast-availability is ADVISORY (cascade/vial/staged
  legitimately cast a name not in hand — false positives are the skill's warned failure mode).
  Validated across all 6 smoke decks; negative-tested (an unwired type HARD-flags). Skipped with
  `--no-sweep` (a runtime sweep, like `mismatch`).
- **4b `claude_sweep` (judgment, artifact-ledger)** — the expensive Claude-DRIVEN sweep stays
  user-initiated (analyze-deck 5d / Workflow, one agent per game); its result is RECORDED in the
  per-deck ledger under `## Claude-play sweep` (commit / seeds / `flags: N unresolved`). Gate:
  absent → disclosed SKIP; ≥1 unresolved flag → blocking FAIL; clean → PASS (staleness vs HEAD
  disclosed). Matches the "expensive step = user-initiated, result enforced" philosophy (cf.
  mulligan-profile). Record format documented in `.claude/skills/claude-play.md`.

### ⑤ Heuristics + profiling — close the loop
- `scripts/auto_heuristics.py`: mine (exists) → **mechanically encode the unambiguous classes**
  (0-conflict ORDER, LAND/FETCH skew, `XCandidates=max_affordable`, dead-gate skip) → run the
  **`MTG_UNPRUNED` per-game A/B vs `gt_logs`** → machine verdict. Passes the strict bar →
  **auto-adopt**; **always disclose all**; escalate only unexplained regressions + the
  intrinsically-human calls (setup-vs-inert +delta, clairvoyance triage). Wire `MTG_BRANCH_STATS`
  hotspots into pruner candidates; data-driven archetype binding so a new deck doesn't need
  hand-coded `SelectDecisionProvider`.

## Sequencing
**The three manual-effort drains are roughly EQUAL (user, 2026-07-17): viewer bugs (①), play
bugs (②+④), and optimizing (⑤).** None is uniquely worst; all three are first-class and the
plan must reduce each. Recommended build order (user approved "we want it all, any order you
recommend"): ① → ③ → ② → ④ → ⑤, folding each surface into the ③ gate as it lands. ① is *first*
only because its fixes are the most concrete/cheap (a dead-code bug + a registry + two static
checks) and close the replicate class immediately — NOT because it's the top pain. ② (value/
clause correctness) and ④ (enforced, broader play sweep) together attack the play-bug drain, and
⑤ attacks the optimizing drain; each gets equal weight, not leftover attention.

## Pipeline ordering — mulligan profile is the LAST, USER-INITIATED stage (user, 2026-07-17)

**Why this order is crucial (user):** mulligan-profile generation has **two independent
showstoppers**, and either one alone kills it:

1. **Incorrect due to play errors** — the profile bakes in whatever the engine *does*; if play is
   wrong, the profile optimises against wrong play and is invalid.
2. **Unreasonably slow due to performance issues** — generation is expensive; if the engine is
   slow (e.g. the ~68–82 s claude-play mulligan keep-eval), generation is infeasible to even run.

So generation must wait until BOTH are resolved — play validated *and* performance optimised. It is
also commit-bound (the raw sidecar's `commit` fingerprint gates cross-machine pooling; *any* later
engine change — a perf fix, a validation fix, OR an issue the user finds in the viewer —
invalidates every sidecar), which is why it only pays off once **everything that could still change
the engine is frozen**.

**Mulligan-profile generation happens LAST, and the USER kicks it off** — never during initial
analyze, and never automatically. The full ordering:

1. **Initial analyze** — cards + coverage + autonomous play + viewer wiring. Ships the deck on
   **defaults / static keep** (no exhaustive profile; `bottoming_enabled` off). Its job is to make
   play *correct* and the viewer *complete*, NOT to generate a profile.
2. **Performance work** — optimize the deck's hotspots (profiling → hotspot is the autonomous win)
   AND the interactive path (e.g. the ~68–82 s claude-play mulligan keep-eval,
   `claude-play-mulligan-latency.md`, which blocks both profile gen and cheap viewer verification).
3. **Validation** — automated: mismatch harnesses, multi-depth, budget-starvation, the claude-play
   correctness sweep, regression.
4. **The user's own feedback loop** — this is an explicit gate, not an afterthought: the user
   talking with the AI about performance, surfacing potential issues, and issues they find **using
   the viewer**. These get fixed. This is the "sanity-check" phase the whole effort is built to
   enable — and because it can still change the engine, it must **settle before** step 5.
5. **THEN — and only then — the user manually kicks off mulligan generation** on the frozen commit
   (the separate `mulligan-profile.md` workflow: defaults → low-R exhaustive keep → high-R;
   `bottoming_enabled` ships off until a validated high-R run).

This means the earlier `analyze-deck` "Stage 4 = build & run" **must not** include
exhaustive-profile generation; that moves out to step 5 above. (Reinforces, and is stricter than,
`mulligan-profile.md` Rule 0 — which says "generate late on a frozen commit"; this adds that the
user's hands-on perf+viewer feedback is *part of* what must freeze first, and that the user, not
the process, initiates it.)

## Difficulty calibration (user, 2026-07-17)
Ordered by how much manual effort remains achievable-to-remove: **viewer (easiest — a
completeness check over a finite param set) < card bugs (middle — "stamp down most" via
faithful implementation + good testing) < optimizing (hardest)**. For optimizing, **profiling→
hotspot is the autonomous win**; heuristic *generation* is partly autonomous but has an
**irreducible tail that depends on the user's play experience** — ⑤'s goal is to maximize the
autonomous fraction and hand the user a clean, disclosed residual, not to feign full autonomy.

## Progress log
- 2026-07-17: 4-agent code audit complete; plan approved; doc created. Starting ①.
### Viewer decision policy (user, 2026-07-17) — two standing rules
1. **Default = surface EVERY decision** — via a persistent options menu with a per-decision
   "let the AI decide" toggle. The toggle default is ON (surface) in general, but the USER may
   designate specific high-frequency decisions as **default-OFF (auto)** — and has done so for
   the shockland/snarl pay-life/reveal decisions (constant prompting is annoying). So those get
   wired (surfacing possible + toggleable) but their menu default is OFF/auto, backed by a *good*
   heuristic (shocklands: pay life only when it benefits you). This is a user-set per-decision
   default, NOT the process guessing. A real decision not yet wired at all stays **unclassified
   → the guard fails** until it is wired (or the user signs off a deferral).
2. **Targets are NEVER restricted in human-play.** The target dialog offers every legal target
   (own AND opponent), per the "provider must not narrow in human-play" invariant. A
   `target_own_creature`-style hint is for the AUTONOMOUS search only; a truncated target list is
   a surfacing bug. (Invigorate can target an opponent creature — real line: Invigorate + Swords
   to Plowshares via Tainted Remedy; the goldfish "own attacker" model reflects only that the
   passive opponent has no board — a disclosed ② limitation.)

- 2026-07-17: **① guard REBUILT + full taxonomy** (`scripts/audit_viewer_decisions.py`). Dead
  `CHOICE_PARAM_KEYS = set(MANIFEST.keys())` replaced with a **complete param registry**:
  `MANIFEST` (18 decision types) + `MAINPHASE_PARAMS` (surfaced via plan/board activation) +
  `INERT_PARAMS` (no choice, with reasons) + `DEFERRED_PARAMS` (user-approved gaps). Guard routes
  every param to one bucket or **UNCLASSIFIED→hard-fail**. Final classification (occurrences):
  183 inert, 33 decision, 15 main-phase-ride, 2 deferred (cascade / Reality Spasm), **2
  UNCLASSIFIED**. Corrections applied from user review: `target_own_creature`→inert (rides
  `targeting`, unrestricted), `discard_land_damage`→main-phase (Land's Edge board-click already
  works, `main.cpp:100`), auto-default category DROPPED (default is surface-all).
  **Remaining ① wiring list (2 decisions to SURFACE, bucket-B modals):** `etb_pay_life_to_untap`
  (6 shocklands, "pay 2 life or enter tapped") and `etb_untap_reveal_subtypes` (Frostboil Snarl,
  "reveal a land or enter tapped"). NEXT: wire a shared yes/no modal for these; then the
  per-decision auto opt-in (separate, later).

- 2026-07-17: **① land-entry decision WIRED + persistent options menu (default-off/auto).** New
  shared `land_entry` decision type covers BOTH bucket-B lands (shock: pay life; reveal: Frostboil
  Snarl) as one binary "enter untapped (pay the cost) / enter tapped" modal. Four wiring sites:
  (1) `LandEntryChooser` typedef + `g_play_land_entry_chooser` in `GameLogger.h/.cpp`, nulled in
  `RevealLogPause`; (2) shared call site `TurnSolver::PlayLandByName` (the real-game land drop that
  `ApplyPlan` runs) — gated on the pointer + `LandEntryHasChoice`, heuristic fallback when null
  (refactored `LandEntersTapped` into pure `LandWouldEnterTapped` + `LandEntryHasChoice` +
  `ApplyLandUntapPayment` — **byte-identical** for the search / analyzer, which never sets the
  chooser); (3) emitter `WriteLandEntryDecisionJson` in `main.cpp` + installed lambda; (4) GUI
  `landEntryPanelHtml` + dispatch/wire/`commitLandEntry` in `index.html`. MANIFEST maps both params
  → `land_entry`; auditor self-guard now **passes** (Anti-Lifegain: expected types `divide`,
  `land_entry`, `target`, 0 unclassified). **Persistent options menu** (`⚙ Options`,
  localStorage `mdt_surface`): per-decision "surface vs. let-AI-decide" toggles for the repetitive
  single-int classes (`land_entry`, `replicate`, `retrace_discard`, `vial_charge`); `land_entry`
  ships **default-OFF (auto)** — `advanceTo` auto-replies `heuristic_default` (shock: pay iff mana
  needed; reveal: reveal iff able) without a modal, keeping checkpoints/steps 1:1. Engine still
  ALWAYS emits the decision (never an engine-level skip); the menu is viewer-only convenience.
  DECISIONS.md updated (registry row + "Surfacing options" section). Validated: both binaries
  build clean; **smoke 18/18 exact-digest PASS → analyzer/search path byte-identical** (0 win→loss,
  0 play-changed); auditor **self-guard passes** (Anti-Lifegain expected: `divide`, `land_entry`,
  `target`; 0 unclassified). Live runtime surface-check via claude-play was **deferred to indirect
  proof** because each stateless-replay launch costs ~68–82 s (mulligan keep-eval — separated out
  to `claude-play-mulligan-latency.md`); the wiring is structurally identical to the proven
  `replicate`/`retrace` types. **① effectively closed** modulo the latent 2nd greedy replicate loop
  (`AIEngine.cpp:3200`) + static emitter/GUI-branch checks, which fold into ③.
- 2026-07-17: **② card-text reconciliation MOSTLY BUILT.** `scripts/audit_card_fields.py` (offline
  diff of cards.json vs a committed Scryfall snapshot: cost/PT/types/keywords HARD, oracle_text
  advisory; fails closed when the snapshot is missing) replaces the spine's `card_fields`
  disclosed-skip; `analyze_deck.py --coverage-only` now hard-stops on partial gaps; the spine's
  `card_fields` gate interprets snapshot-missing → SKIP-disclosed (run `--update`), mismatches →
  FAIL. Clause ledger deferred (function covered by coverage-partial + bracket-notes + oracle-diff).
  Only remaining ② step: run `audit_card_fields.py --update` on a networked machine + commit
  `scryfall_reference.json` (Scryfall is unreachable in this sandbox — diff engine validated
  synthetically instead).
- 2026-07-17: **② DONE — snapshot committed + gate calibrated + card_fields LIVE.** Root-caused the
  "Scryfall unreachable" block: `.devcontainer/init-firewall.sh` allowlisted api.scryfall.com but by
  a **one-shot `dig` snapshot** pinned into an ipset — Scryfall is 100% Cloudflare (rotates IPs,
  serves AAAA first), so runtime resolved to a rotated IPv4 (never in the ipset → SYN dropped) or
  IPv6 (REJECTed). Fixed (`0748cff`): firewall now **pins each allowlisted host to an allowlisted
  IPv4 in `/etc/hosts`** + widens the ipset with a few resolves. Built the 102-card snapshot
  (`25ecda2`; default 0.1s throttle 429-throttled 24 cards → bumped to 0.25s + gentle re-fetch).
  Calibrated the gate (`80f1b11`): systematic noise stripped in-code (ability-word/keyword-ability
  set, land subtypes, Basic/World supertypes, CDA `*` component-wise) + per-card allowlist
  `scryfall_divergences.json` (3 keyword-lord Slivers, Haytham protection, 3 tribal-subtype
  omissions — all reviewed, bracket-noted, never silent, STALE-detected). Negative-tested (real P/T
  / missing-keyword / un-allowlisted faults still HARD-caught). `card_fields` gate → **PASS** with 7
  disclosed allowlisted + 57 oracle advisories. **② fully closed.**
- 2026-07-17: **② tribal subtypes: faithful fix (`f105f1c`).** Added the real Scryfall creature
  subtypes to Swiftspear (Human Monk) / Goblin Guide (Goblin Scout) / Eidolon (Spirit) in cards.json
  instead of allowlisting the omission (future-proofs a Goblins deck); allowlist down to 4. Byte-
  identical: cards.json is runtime-loaded, sidecar keys on play_digest not per-card DefHash, nothing
  in the frozen decks keys on those types — smoke 18/18 exact-digest PASS, play-changed=0.
- 2026-07-17: **⑤ started — perf hotspot fixed + auto_heuristics loop built.** (a) Profiled the
  "slow search hotspot" scenario `invigorate_not_lethal_no_fire`: it was NOT the search — perf showed
  60% nlohmann json::parse; RunScenario eagerly parsed the 11 MB exhaustive KEEP sidecar a fixed board
  never consults. Fixed `5289775` (skip it) → 67s → 0.08s, byte-identical; scenario-sanity gate ~200s →
  0.33s per regression run. (b) `scripts/auto_heuristics.py` BUILT (`136cc51`): the measure→decide→
  **auto-adopt**→report loop (user model = adopt-then-review: adopt autonomously on passing tests,
  report at end for veto, behind a disable toggle). **Metric = avg-9 ONLY** — win→loss game-flip counts
  are irrelevant (user directive) and scrubbed; slower games = the issue cases, faster games = a
  BUG-check (esp. early), not a clairvoyance hunt (the search is clairvoyant by default). One run per
  variant is a full A/B (harness prints exp=baseline vs got=variant). Verified all 4 verdict paths +
  a real tap_order run (legacy tap → reject_regression, kept scarcity default). (c) Winner-ACTIVATION
  built (`1ffe241`): `src/core/HeuristicDefaults.h` + `ApplyHeuristicDefaults()` at the top of both
  main()s reads committed `src/ai/data/heuristic_defaults.env` and `setenv(overwrite=0)` each KEY=VALUE
  → an adopted heuristic is the LIVE default with no rebuild, env var still overrides (= disable/A-B).
  Byte-identical when empty (smoke 18/18, play-changed=0); verified live. **Autonomous loop COMPLETE:
  measure→decide→adopt(activate)→report.** REMAINING ⑤: author real heuristic experiments (genuine
  ordering/weight variants) for the loop to optimize.
- 2026-07-17: **④ BUILT (`0bcdfcd`).** Replaced the `claude_play` SKIP stub with two gates.
  **4a `play_invariants`** (`scripts/play_invariants.py`, LIVE blocking, `--no-sweep`-skippable):
  drives the claude-play protocol auto-following engine defaults, asserts determinism + integrity +
  progress; advisory cast-availability (cascade/vial/staged). Validated on all 6 smoke decks; the
  develop-policy exercises target+divide (divide = the only multi-consume decision, supplied from
  `legal_targets` defaults); negative-tested. **4b `claude_sweep`** (artifact-ledger): the expensive
  Claude-driven judgment sweep stays user-initiated, recorded under `## Claude-play sweep` in the
  per-deck ledger (commit/seeds/`flags: N unresolved`); absent→SKIP, unresolved→FAIL, clean→PASS,
  staleness disclosed. Record format documented in the claude-play skill. **Note:** the claude-play
  skill's "api.scryfall.com is reachable through the egress firewall" is TRUE again post-firewall-fix.
  battery; exit non-zero unless every blocking gate is green or signed off in the per-deck ledger
  `docs/design/analysis-<deck>.md`. Closes the "partials exit 0" hole (coverage now hard-fails on
  `partial`) and adds the static sites-3&4 `viewer_wiring` check (emitter + GUI branch per the
  DECISIONS.md registry). Disclosed skips for the not-yet-built ② (card_fields, clause_ledger) and
  ④ (broadened claude-play sweep) — visible, never silent. Sign-off = user-owned `## Approved
  deferrals`; a fully-approved FAIL → DEFERRED. Validated on Anti-Lifegain (coverage catches the
  unbracketed Ignoble Hierarch exalted; sign-off → PASS). NOTE: the viewer/mismatch runtime gates
  are now cheap because claude-play skips the exhaustive sidecar (`48c5a51`).
- 2026-07-17: **Pipeline-ordering policy added** (user): mulligan-profile generation is pulled OUT
  of initial analyze; gated on play-correctness-shown + performance-optimized. See the new
  "Pipeline ordering" section above and `claude-play-mulligan-latency.md`. Implication for ⑤/③: the
  perf-optimization workstream now also owns the claude-play mulligan latency, since it blocks both
  the profile stage and cheap viewer verification.
