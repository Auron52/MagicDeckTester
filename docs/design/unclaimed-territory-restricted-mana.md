# Faithful restricted-mana for Unclaimed Territory / Cavern of Souls / Secluded Courtyard

**Status: ADOPTED 2026-07-22.** The four `colored_creature_only` flags are now applied to cards.json
(Unclaimed Territory / Cavern of Souls / Secluded Courtyard / Sliver Hive: `produces` gains `{C}` last +
`colored_creature_only: true`) on top of the previously-committed engine wiring, and smoke+regression GT
is rebaselined on the resulting behaviour. Adopted after the post-merge, prune-on re-A/B + a per-game
trace review of every persistent d5 slowdown confirmed the user's gate: the searched cost is tiny
(dragonstorm +0.03..+0.05 turns, knights BYTE-IDENTICAL, slivers ~neutral +0.0067) and is **dominated by
honest LOAD-BEARING faithful corrections** — games where the baseline was illegitimately powering out an
early combo off Unclaimed's fake red with little or no real red on board. The purest example is
`d5 s3003 gi171`: the baseline combos on T4 off **two Unclaimed Territories with ZERO real red anywhere**,
a 100% modeling-bug "win" that the fix correctly delays until real red is drawn (~T7). The few larger
non-load-bearing gaps (e.g. `d5 s2002 gi63`, where B assembles dragons by T5 but the search fails to close
until T8) are exactly the "search failing for lack of a value-leaf" cases the user flagged; their
recoverability under sufficient search is to be demonstrated empirically by re-measuring this A/B WITH the
Dragonstorm value-leaf once it is generated. Full prune-on gate evidence: `logs/unclaimed_ab2/`
(cards_fix.json, per-case A/B, per-game A-vs-B traces, render_ab.py classification).

Design: `colored_creature_only` produces gain `{C}` (placed LAST so the generic-tap colour for CREATURE
casts is unchanged); backtracker sites use `ProducesForPayment` (SpellEffects.h) to strip the colours for a
non-creature spell. Verified: Dragonstorm rituals / Dragonstorm / Apex can no longer take a red pip off
Unclaimed (combo correctly delayed); Slivers/Knights (all-creature decks that rely on these lands for
coloured mana) stay ~neutral -> the for_creature path is intact.

Design (unchanged, for when adopted): `colored_creature_only` produces gain `{C}` (placed LAST so the
generic-tap colour for CREATURE casts is unchanged); backtracker sites use `ProducesForPayment`
(SpellEffects.h) to strip the colours for a non-creature spell. Verified: Dragonstorm rituals /
Dragonstorm / Apex can no longer take a red pip off Unclaimed (combo correctly delayed); Slivers/Knights
(all-creature decks that rely on these lands for coloured mana) stay ~neutral -> the for_creature path is
intact.

## DEFINITIVE per-case regression proof (2026-07-22) — autonomous A/B, 0 stumbles

The user (rightly) refused "correct + faithful" without per-case proof of *where the restricted line fails
to commit the original line*. Done, authoritatively, for EVERY regressed game in all 5 regression cases
(d0/d3/d5 × s2002/s3003). Artifacts: `logs/unclaimed_ab/` (FINDINGS.md, 45 saved baseline traces under
`regressed_baseline_logs/`, the A/B runner + classifier scripts).

**Clean A/B isolation.** The fix is runtime-data-gated: one binary; behaviour toggled purely by
`--cards-json` (BASELINE = flag off, FIX = flag on). Proven: BASELINE-cards reproduces the committed GT
digest EXACTLY for all 5 cases (d0 8.0640/74b3fa…, d3 4.9667/f257… + 4.9067/34ce…, d5 4.8080/b64d… +
4.8560/4d3c…) → the solver code is genuinely INERT with the flag off, so A_traces ARE the committed
baseline lines and any B delta is 100% the mana model.

**METHOD CORRECTION (important).** First attempt used `--validate-line`/`CheckLine` via claude-play to ask
"is the original line committable in B?". It reported `accept` everywhere — INCLUDING the fake-red games.
That was a FALSE method: claude-play runs MTG_HUMAN_PLAY, whose apply path EXECUTES the optimistically-
enumerated plan's payment (it force-cast Rite of Flame off Unclaimed alone in gi322). The autonomous
engine does NOT — it re-validates through the restricted backtracker and correctly drops the cast.
Verified directly: autonomous B at d0 gi322 played Unclaimed T1 and cast NOTHING, deferring the combo to
T2 after playing a Mountain. So the fix WORKS in real (autonomous) play; the claude-play validator is
unreliable for fake-red lines and was discarded. (CheckLine's `plan_pays` trial-applies via ApplyPlan,
which under HUMAN_PLAY is the optimistic executor — that is the leak.)

**Authoritative classifier (real-red-at-ignition, from autonomous traces).** A storm ritual needs a red
pip to cast; once one real red source ignites the first ritual, the chain self-fuels (Unclaimed's `{C}`
covers generic). So at each game's first A-vs-B divergence, read the untapped real-red sources on the
board (Mountain=1, in-play Sandstone=2, charged storage = its storage-counter count [read from the trace's
`counters`], in-play Lotus=3; Unclaimed=0 under the fix) plus the line's own land drop:
- `real_red == 0` → **FAITHFUL**: the original line's first red pip had ONLY Unclaimed to pay it → now
  genuinely unpayable. The fix correctly biting; the exact unpayable cast is nameable (e.g. gi322: Rite of
  Flame `{R}` at T1, only Unclaimed in play).
- `real_red ≥ 1` → **IGNITABLE**: ignition available off a real source; autonomous B's search DEFERRED to
  a slower line (search variance, or a deeper full-line red-shortfall where B waits to draw/play a
  Mountain — e.g. gi169/gi79). NOT a stranded-real-red stumble (B always still won, within 1–3 turns).

**Result across all 45 regressed games: FAITHFUL 26 · IGNITABLE 18 · land-seq-only 1 · STUMBLES 0.**
- **All 5 WIN→LOSS games are FAITHFUL** (d0 gi91/287/459/933 = T8 fake-red wins; d3-s3003 gi171 = T4) —
  the "lost" wins were pure modeling-bug artifacts (they leaned on Unclaimed's fake red at ignition).
- **d0 (greedy, NO search — the purest mana signal): 11/11 FAITHFUL.** With no lookahead to blur it, every
  regression is directly the fake-red correction.
- **0 stumbles** anywhere: no game where a real red source was available/strandable and B failed a line it
  should have paid — matching the user's prior that genuine stumbles should be ~zero if the fix is correct.

Verdict: the fix is CORRECT (no {C}-count bug, no ordering bug, no stumble) and FAITHFUL (the regression is
fake-red artifact wins being removed + the low-depth/greedy search re-finding lines under a more accurate
model). The +0.05 d5 aggregate is dominated by legitimate faithful corrections plus benign search
deferral. Remaining decision (below) is purely whether to adopt the more-accurate model into GT.

## Regression + bug investigation (2026-07-22 — superseded by the DEFINITIVE proof above)

After implementation, the full regression showed a LARGER Dragonstorm slowdown than expected
(d5 s2002 4.808->4.860 +0.052, d5 s3003 4.856->4.892 +0.036; ~44 searched-slower vs 17 faster; one
win->loss gi171 T4->loss). Slivers ~neutral, Knights unchanged. The user's prior: the deck has ~22
red sources (Lotus + all lands) and Unclaimed is a 2-of, so a *big* regression smells like a BUG
(their leading hypothesis: Unclaimed not being counted as {C}; secondary: a tap-ordering bug where the
solver spends a red source on a generic pip and strands the {R} pip, "should back up when it doesn't
work out" — flagged especially for the storage lands Mercadian Bazaar / Dwarven Hold).

**Bug hypotheses TESTED and DISPROVEN** (fixtures saved under test/scenarios/, all PASS):
- `unclaimed_c_necessity` — 8 Mountains + 1 Unclaimed; Dragonstorm {8}{R}=9 is payable ONLY if
  Unclaimed contributes its {C} (8 Mtn = 8 < 9) with the {R} pip taken from a Mountain. PASS -> {C}
  IS counted; ordering OK.
- `unclaimed_tap_ordering` — 1 Mountain (only red) + 1 Unclaimed, ONLY {1}{R} rituals (no pure-{R}
  Rite). Pyretic #1 must pay {1} from Unclaimed {C} and {R} from the lone Mountain. PASS -> scarce-red
  ordering OK.
- `unclaimed_storage_ordering` — 1 CHARGED Mercadian Bazaar (2 storage -> bursts {R}{R}) + Unclaimed;
  Seething {2}{R} must pay part of {2} with {C} and save a red for {R}. PASS -> storage-burst ordering OK.
- (also ad-hoc: 2x Sandstone depletion + Unclaimed Seething chain -> PASS.)
The solver's greedy pass has a BACKTRACKING FALLBACK (TurnSolver.cpp ~2794 "Greedy-first, then a
backtracking fallback"), which is why these resolve. So there is NO {C}-counting or tap-ordering bug
for Mountain / Sandstone(depletion) / Mercadian(storage). Dwarven Hold (storage) not yet isolated but
shares the storage_land burst path with Mercadian.

**Sampled regressed games are LEGITIMATE** (explains): gi91/gi129 (T1 = uncharged Mercadian storage,
no fast red -> OLD cast T2 rituals off Unclaimed's fake red), gi171 (opening = 2 Unclaimed + Ruby,
genuinely NO real red -> old T4 win was pure modeling-bug artifact; faithful = loss), gi13 (T1
Sandstone real red -> OLD cast Ruby+Pyretic; Pyretic WAS payable off Sandstone, the NEW d3 search just
chose Ruby-only = shallow-search variance, not a mana failure). Pattern: the deck's OTHER red is often
SLOW (storage lands need charging; Lotus Bloom is suspended 3 turns), so the ~13 FAST red sources
(9 Mountain + 4 Sandstone) mattered more than the raw 22 count; Unclaimed was fast fake-red.

**REAL-CASE ANALYSIS DONE (verdict: correct, no bug, no leak).** Reconstructed the actual regressed
games via a 1-job manifest + `--game-trace-dir` (writes `<trace_dir>/<job>_gi<N>.json` per game;
openingHand + per-turn actions). Findings:
- **No leak:** all 22 changed d5_s2002 games contain an Unclaimed Territory somewhere -> the fix never
  touches a game without a colored_creature_only source (as designed). (A game I first traced, gi123,
  turned out to be a CAST-PRIORITY-change game, not an Unclaimed one -- different regression run.)
- **gi13** (d5 5->6, "hand+draws IDENTICAL"): opening = Lathliss, 2x Mountain, Ruby, Unclaimed, Lotus,
  Sandstone Needle -> RED-RICH (2 Mtn + Sandstone). The combo IS castable off real red; the d3/d5 search
  just sequenced it 1 turn slower = benign SEARCH VARIANCE, not a payability failure.
- **gi171** (d3 T4->loss): opening = 2 Unclaimed, Lotus, Ruby, 2 Seething, Scourge -> NO fast red at all
  (only the 2 fake-red Unclaimed + suspended Lotus). Faithfully unpayable early; old T4 win was a pure
  modeling-bug artifact. Loss is CORRECT.
- **Split** of 8 sampled slower s3003 games: 6 RED-RICH (variance) / 2 red-poor (legit faithful). So the
  aggregate +0.05 OVERSTATES the faithful impact -- much is benign shallow/deep search churn in hands
  that have the red. The user's prior ("a 2-of shouldn't be costly") is largely borne out; the true
  faithful regression (red-poor hands genuinely leaning on Unclaimed's fake red) is a minority.

**(2026-07-22: RESOLVED — adopted; see the header. Retained as history.)**
**DECISION PENDING (user):** rebaseline smoke+regression GT (the fix is correct + faithful) vs hold vs
revert. Uncommitted in the working tree: CardDatabase.{h,cpp} (colored_creature_only), SpellEffects.h
(ProducesForPayment + backtracker strip), TurnSolver.cpp + AIEngine.cpp (call sites), cards.json (4
lands, {C} last), src/main.cpp (scenario storage_counters/charge_counters), test/scenarios/unclaimed_*
(3 fixtures), this doc. GT NOT rebaselined (suite red on Dragonstorm/Slivers). Optional follow-up: the
red-rich d3/d5 variance is a search-quality artifact (not a bug) -- could be reduced by higher budget.

**Known limitation (deliberate):** only the authoritative BACKTRACKER (tap/pay) enforces the
restriction. The pool fast-paths (BuildNonCreaturePool / AddSourceToPool) still count a
colored_creature_only source's colours for a non-creature -> they can be optimistic (enumerate a line
the backtracker then drops = benign over-generation). The mana-VALUE MaxManaGate is unaffected (these
lands do make 1 mana of *some* colour). No fd-diverge observed on the tested decks. Tighten the pool
sites only if a future deck shows a predict-vs-realise mismatch.

## The gap

Three "tribal any-color" lands are modeled as **unrestricted 5-color** (`produces:[W,U,B,R,G]`,
no restriction), per their card comments in `cards.json` ("Simplified: modelled as producing all
five colors; ETB choice and color restriction not modelled"):

| Card | Real Oracle |
|------|-------------|
| Unclaimed Territory | choose a type; `{T}`: Add `{C}`. `{T}`: Add one mana of any color — spend only on a creature spell of the chosen type. |
| Cavern of Souls | as above + "that spell can't be countered". |
| Secluded Courtyard | as above (+ activate an ability of a creature source of the chosen type). |

Each is really a **mixed** source:
- `{C}` — unrestricted (pays any generic cost), AND
- one **colored** mana — usable ONLY to cast a **creature spell of the chosen type**.

The engine currently lets the colored mana pay **anything** — so e.g. in the Dragonstorm deck
Unclaimed Territory wrongly pays the `{R}` pip of a ritual / Dragonstorm / Apex. Reality: it can
pay their **generic** with `{C}`, but the **red pips** must come from real red sources
(Mountains, rituals, Lotus Bloom). Net effect of the gap: the deck's mana reads **easier** than it
is, so the engine's Dragonstorm evaluation/play is optimistic.

## Existing machinery

`creature_mana_only` (bool) already exists — **Ancient Ziggurat** uses it. Semantics: the source
may be tapped ONLY when paying for a creature spell (`for_creature`). Checked at ~6 mana-solver
sites: `if (def.params.creature_mana_only && !for_creature) { reject/continue; }`
(`AIEngine.cpp:2673`, `TurnSolver.cpp:285/2521`, `SpellEffects.h:3936/3973`, sim-key at
`TurnSolver.cpp:6167`).

But `creature_mana_only` makes **ALL** of the source's mana creature-only — Ancient Ziggurat has no
`{C}` ability, so that's correct for *it*. Unclaimed/Cavern/Courtyard need the `{C}` escape too, so
the flag can't be reused as-is (setting it would wrongly forbid paying generic with `{C}`).

## Recommended approach

Add a new flag `colored_creature_only` (bool). Semantics: **`{C}` is unrestricted; the COLORED
mana is creature-only.** Model these lands as `produces:[C,W,U,B,R,G]` + `colored_creature_only:true`.

At each of the ~6 solver color-match sites, when the spell being paid for is **not** a creature (of
the chosen type): allow the source to satisfy only a `{C}`/generic need, never a colored pip. When it
**is** a qualifying creature: all colors are usable (same as today).

**Chosen creature type — simplify to "any creature".** These lands are played in single-tribe decks
(Dragon for Dragonstorm, Sliver for Slivers), so "creature spell of the chosen type" ≈ "any creature
spell" for every deck we test. Model the colored restriction as plain `for_creature` (reuse the
existing predicate) and skip per-source type tracking. Note the approximation in the card comment;
revisit only if a multi-tribe deck is ever tested. (Sliver Hive already carries a `tap_token_*`
tribe param but its mana is likewise modeled unrestricted — same treatment applies.)

## Scope / cost

- New param `colored_creature_only` in `CardDatabase.{h,cpp}`; set it on Unclaimed Territory, Cavern
  of Souls, Secluded Courtyard (and consider Sliver Hive) in `cards.json`, add `C` to their
  `produces`. Update the "[Simplified: ...]" card comments to record the new (still-approximate) model.
- Extend the ~6 `creature_mana_only` solver sites to also honor `colored_creature_only` (colored-only
  restriction, `{C}` always allowed). Fold the new bit into the sim-key at `TurnSolver.cpp:6167`.
- **GT-affecting**: nerfs Dragonstorm + Slivers mana → smoke + regression (+ eventually overnight)
  rebaseline. Expect Dragonstorm to get measurably slower (the combo can no longer lean on Unclaimed
  for red pips) — a *correctness* improvement, not a regression.
- Consult `.claude/skills/mtg-rules.md` before implementing (mana abilities / restricted mana).

## Verification

- Unit-ish: at a Dragonstorm state with Unclaimed Territory untapped and no other red, a ritual
  ({R} pip) must NOT be payable off Unclaimed alone; a hard-cast Dragon ({...}{R}{R}) must be.
- Reproduce seed-23 style combos: Unclaimed contributes to generic but red pips still require real
  red — the combo timing should shift later where it previously leaned on Unclaimed for red.
