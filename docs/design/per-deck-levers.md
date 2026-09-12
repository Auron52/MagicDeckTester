# Per-deck levers — the complete current inventory

*Written 2026-09-12, at the user's request: "a list of the current per-deck levers (rather than
constants)".*

A **lever** here means something that is set **per deck** and changes what the engine does. It is the
complement of a *constant*: a constant lives in the source, applies to every deck, and moving it is a
fleet-wide decision. The whole point of the 2026-09-11/12 shape screen was to work out which of these
levers earn their keep as levers and which should collapse into constants — so this file records the
state after that screen, not an aspiration.

Two warnings that apply to the entire table and have each already cost real work:

1. **A key's presence in a sidecar does not mean it is live.** There are three invisible activation
   tiers (§8 of `shape-adoption-decisions.md`). Check the tier column before concluding anything from
   a key being set — `KittyEquipment` ships `target_depth: 5, budget_ms: 20, enabled: false` and is
   entirely inert.
2. **For most of these, PRESENCE IS ADOPTION.** A value sidecar existing activates the value-leaf
   hybrid in play; a keep table existing activates it. `"enabled": false` is *not* an off switch for
   the file, only for the `drives()` tier. To ship a rejected value model, rename it
   `<stem>.value.DISABLED.json`.

---

## 1. Search-shape levers — `<deck>.value.json` → `value_play`

These are the levers the shape screen was about. "Tier" is the activation gate:

* **load** — read whenever the sidecar exists, regardless of `enabled`.
* **`drives()`** — `target_depth > 0 && enabled`.
* **`vp_here`** — `drives() && lookahead_depth == target_depth` (i.e. only at the deck's own depth).

| lever | tier | meaning | who sets it today |
|---|---|---|---|
| `target_depth` | — | the deck's play depth; also the gate for the two tiers below | 13 decks |
| `budget_ms` | — | per-decision budget | 13 decks |
| `enabled` | — | `true` = adopted, drives and locks play. `false` = a recorded recommendation only | 12 true, 1 false (kitty) |
| `ladder` | load | `""`/`"escalation"` = value hybrid; `"single"` = **FIT** one-depth; `"emulated"` = dead (§7) | `single`: **dragons, minotaur** only. `escalation`: fluctuator |
| `leaf` | load | `""`/`"model"` = the learned value model; `"none"` = leafless (`Constant()`) | `none`: dragons, dragonstorm, fluctuator, goblins, minotaur, mirrorwing, stompy, th |
| `alpha` | load | start-gate alpha of a **leafless** probe: `"relaxed"` / `"strict"` | `relaxed` on all 8 leafless decks |
| `fit_alpha` | load | multiplier on FIT's affordability gate. 0 = unset = the strict 1.10x-remaining gate | **nobody** (see §11h) |
| `fit_lazy_r` | load | defer FIT's per-game R calibration. −1 = unset = engine default (**ON**) | **nobody** (see §11h) |
| `commit` | load | emulated ladder's committing pass | **nobody** — dead with `ladder: emulated` |
| `exhaust_mult` | load | how far past budget a leafless pass may run | **nobody** |
| `beam_width` | `drives()` | keep only top-N value-ranked plans near the leaf | antilife, cgiving, dragonstorm, hinata (all 3) |
| `beam_leafdepth` | `drives()` | how near the leaf the beam applies | same 4 decks (all 2) |
| `escalation_cap` | `vp_here` | single-pass predicted-affordable escalation, capped | 12 decks (5 or 6) |
| `escalation_r` | `vp_here` | frozen heuristic cost-per-probe-leaf for that walk | 7 decks (16…240) |
| `escalation_fresh_frac` | `vp_here` | escalation budget renewal. −1 = legacy shared remaining | 7 decks, all 0.5 |
| `regime` | — | **informative only**, never read at runtime | 10 decks |
| `leaf_cost_ms` / `heur_cost_ms` | — | **informative only** — lets `target_depth` be re-derived | a few |

**Four of these are levers only in principle.** `commit`, `exhaust_mult`, `fit_alpha` and
`fit_lazy_r` are set by **no deck at all** — the last two because the Melira adoption that justified
them did not survive the 2026-09-12 rebase (§11h); they are kept as the instrument that diagnosed it.
`regime`, `leaf_cost_ms` and `heur_cost_ms` are never read by the engine at all. And `escalation_cap`
is *provably inert* under `ladder: single`, so on dragons and minotaur it is a key that looks set and
does nothing.

**`escalation_fresh_frac` is the cautionary tale.** Collapsing it to a constant 0.5 was recommended,
implemented, and reverted within a day — it silently overrode `treasure_hunt`'s own later on-policy
measurement, which records 0.5 as *rejected* at 1.083x units. See §3a. That is the general hazard of
turning a lever into a constant: it overrules every deck that measured the other way, and it does so
silently.

## 2. Mulligan-generation levers — same file, same block

Distinct from the play levers above: these configure how the **keep table is generated**, not how the
deck plays. They exist because a slow or large-K deck may want its labelling rollouts run cheaper than
its shipped play settings.

| lever | meaning | who sets it |
|---|---|---|
| `mull_gen_depth` | rollout depth for bucket labelling (0 ⇒ inherit `target_depth`) | 20 decks (1…4) |
| `mull_gen_budget_ms` | rollout budget for labelling (0 ⇒ inherit `budget_ms`) | 20 decks (all 3) |
| `expected_buckets` | **recorded once, thereafter CHECKED.** K decides what a generation *is* (hand space is C(K+6,7)), so the generator refuses on a mismatch rather than silently producing an unpoolable table | 9 decks (11…23) |

## 3. Per-deck *artifacts* — levers by their existence

These are not scalars but whole files, and for each of them **the file existing is the adoption
decision**:

| artifact | what it changes |
|---|---|
| `<deck>.profile.json` | `card_scores` (per-card play/keep weights), `hand_score_threshold`, `keep_model` (decision tree), and the `mulligan` rule block — `min_lands`/`max_lands`, `min_color_sources`, `min_playable`, `required_pieces`, `curve_check`, `bottom_order`, `stop_at` |
| `<deck>.value.json` | the learned value model (`eval_model`), plus `value_leaf_table`, `value_fallback_crossover` (per-committed-depth `take_heuristic_at_hdepth`), `value_no_fallback` — **and** the `value_play` block above |
| `<deck>.keepmodel.exhaustive.profile.json.gz` | the exhaustive bucketed mulligan keep table. Presence = adoption. Bottoming is unconditionally on inside it |
| `<deck>.buckets.json` | the deck's equivalence buckets. **User-only ruling** — never install one on a proposal |
| `<deck>.eval.json` | per-deck evaluator weights, where present |

**One dead file, worth knowing about:** `decks/Hinata2/Hinata2.escgate.json` (a learned escalation-gate
model, mean/std/w/nfeat/order) is referenced by **no source file at all** — only by two design docs. It
is an orphan from an abandoned experiment, not a live lever.

## 4. Process levers (not per-deck, but per-*run*, and easy to confuse with the above)

Worth listing only to keep them out of the table: `MTG_*` env flags and the per-job `ValueArm`
overrides (`esc_fit_alpha`, `esc_fit_lazy_r`, `esc_depth_cap`, `of_wave_share`, …). Precedence is
always **arm > deck > env**. These exist so one pooled `mtg --batch` can run arms that would otherwise
need one process each — env statics are process-global, so without the arm mechanism an A/B degenerates
into a loop of small invocations, which is exactly what the pooling rule forbids.
