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

### ② Card text — mechanical clause reconciliation
- Store the **authoritative Scryfall `oracle_text` verbatim** (separate field / sidecar) and diff
  the working entry against it in the gate — catches fabrication/drift (the Irencrag
  "Add six {R}" class).
- Extend the reality-diff to **P/T, keywords, and other Scryfall-checkable fields**
  (`audit_card_fields.py`), not just cost.
- Per-card **clause ledger**: each oracle clause marked `modeled(param)` / `inert(reason)` /
  `deferred(user-approved)`; gate fails on any unaccounted clause. Turns "split into clauses and
  account for each" into a checkable artifact.
- Make `--coverage-only` **hard-stop on partial gaps**; stop bracket-notes from silencing checks.

### ③ Enforcement spine — one green gate
- `scripts/verify_deck.py <deck>` runs the whole battery (coverage[hard-on-partial], cost+field
  audit, viewer audit[fixed], clause ledger, claude-play flags, nonconv/fd-diverge), emits the
  Stage-6a disclosure, and records results + approved-deferrals to the per-deck ledger
  (`docs/design/analysis-<deck>.md`). **Non-zero unless every check is green or every exception is
  a recorded user sign-off.** This is what lets the viewer session be a sanity check.

### ④ Autonomous play — enforce + broaden
- The claude-play correctness sweep becomes a **required gated step** (in ③), not optional prose;
  broaden past main-phase where feasible; disclose what's unexercised.

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
