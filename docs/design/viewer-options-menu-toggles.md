# Viewer options-menu: per-decision surfacing toggles (deferred)

Deferred future work (user, 2026-07-19). Not being built now — parked here so it is
available to everyone, per the CLAUDE.md deferred-work rule.

## Vision

The play viewer should let the user **toggle, per decision type, whether the engine
auto-resolves it or prompts the human** — ideally *every* choice is individually
toggleable from the options menu. This generalises the existing `land_entry`
localStorage-backed "let-AI-decide" toggle (shocklands / reveal lands, default-OFF,
`scripts/audit_viewer_decisions.py` MANIFEST note + `tools/play/DECISIONS.md`) to the
whole decision taxonomy.

The engine ALWAYS emits every decision (surfacing is never silently skipped at the
engine level); the toggle lives in the VIEWER (localStorage-backed) and only controls
whether the human is prompted or the engine's heuristic answer is auto-applied.

## Per-decision defaults the user specified (Dragonstorm)

| Decision (param) | Card(s) | Default | Notes |
|---|---|---|---|
| firebreathing pump (`firebreathing_cost`) | Scourge of Valkas | **ON** (prompt) | "should be a choice the user can make"; rarely changes outcome |
| team pump (`team_pump_cost`) | Lathliss, Dragon Queen | **ON** (prompt) | same rationale as firebreathing |
| ETB ping target (`dragon_ping_on_enter`) | Scourge of Valkas | **OFF** (auto → face) | any-target collapses to face vs the passive goldfish; matters more in phase-2 with a real opponent |

These three are currently **search-resolved** (the engine picks pump amount / ping
target). They are classified as `DEFERRED_PARAMS` in `audit_viewer_decisions.py` (disclosed
in Stage 6a, gate-passing) until this toggle system is built and they are wired as real
viewer decisions with the defaults above.

Mana-color floats (`impulse_float_amount` on Apex of Power, `sac_for_mana_amount` on Lotus
Bloom) are deliberately **left to the engine** (no chooser) — the search picks the optimal
color; classified inert.

## Scope when built

- A per-decision-type toggle registry in the viewer options menu (localStorage-backed),
  seeded with the defaults above (and default-ON "surface" for everything else per the
  post-onboarding "surface every decision" rule).
- Wire the three deferred decisions (firebreathing, team pump, ping target) as real
  emitted/handled viewer decisions, then flip them from `DEFERRED_PARAMS` to `MANIFEST`
  (mapped + surfaced) in `audit_viewer_decisions.py`.
- Phase-2 relevance: with a real opponent, the ping target and pump amounts become live
  tactical choices (which creature/face to ping, how much to pump through blockers).
