# Search recoverability audit — what high-depth/high-budget can and cannot fix

**Status: INITIAL INVENTORY (2026-09-03). Charter from the USER, same day:** *"recoverability is
a crucial aspect of my design here. When things get non-obvious I want to have the option to run
high-depth high-budget and be confident we don't have a bunch of lossy logic messing up the
search."* The invariant: **the search must converge at the limit** — any logic whose error does
NOT vanish as depth/budget grow violates the design regardless of measured frequency at shipped
settings (the same bar as the recorded "infinite-budget test" that rejected beams/width caps).
The corollary ruling (2026-09-03): playout-layer flaws (greedy rollout policy, weak heuristics,
mana-tap rules) are acceptable BECAUSE budget dilutes them; searched-structure flaws are not.

This doc is the inventory. Each entry: what it is, and whether the limit recovers it. Entries
marked NEEDS-VERIFICATION are honest gaps — presumed from design reading, not proven by an
unlimited-budget A/B.

## Recoverable BY DESIGN (budget lifts the cap)

* **Breakpoint width W=2 (`MTG_BP_SEARCH`).** Not a hard cap: `MTG_BP_WAVES=1` (default) runs
  the wave phase "while the node's budget allows; unlimited => exhaustive", opening ranks
  2..n and nested breakpoints (bp_at = 1, 2, ...). A budget-skip of the phase increments
  `g_fs_trunc_events`, which demotes any enclosing no-win from refutation to unknown — the
  anytime contract. NEEDS-VERIFICATION at the limit: an unlimited-budget run on a small sample
  should show waves visiting every rank (compare vs `MTG_BP_SEARCH=<large>`).
* **EnumGroupCap drops.** Group-wave tranches re-open dropped groups under the same budget
  contract (an exhausted-budget skip IS a truncation and is counted).
* **Rollout/playout policy (greedy s90 leaf, `SimulateToEnd`).** The layer the ruling exempts:
  deeper search relies on it less by construction.
* **Value leaf.** Replaces the horizon rollout only; more depth pushes the horizon further out.

## Sound at any budget (identity, not policy)

* **Canon continuation (`MTG_BP_CANON_CONT`, tight scope).** ACT-vs-PASS judged by the greedy
  path's own Solve at the same state; PASS falls through verbatim; "cast nothing" reachable
  (the NGC lossiness this lever exists to fix). The searched structure's continuations are the
  full enumerated list at hosted sites (node) — not a width-limited pick.
* **Enum memo / Solve memo / verdict memo.** Keyed identities with VERIFY harnesses
  (`MTG_ENUM_MEMO_VERIFY`, `MTG_SOLVE_MEMO_VERIFY`); digest-checked. A key bug is a bug, not a
  policy; the harnesses exist to catch it.

## Measured ZERO at ship settings (watch, don't fear)

* **Masked breakpoint classes.** With SITE3 adopted the effective mask covers every observed
  class: `fell-to-greedy: class-masked 0` on every instrumented deck (a masked class at a ROOT
  apply WOULD be unrecoverable greedy — the counter is the tripwire; keep it in any future
  audit run).
* **Empty-cands fallback.** `empty-cands 0` everywhere measured — the enumeration always offers
  a continuation where the searched path needs one.

## NOT budget-recoverable — the honest watch-list (NEEDS-VERIFICATION each)

* **Heuristics wired as PRUNES in the searched part.** The standing doctrine wires judgment
  heuristics as prunes; a HARD prune (drops a line) is exactly the class the invariant forbids
  unless exempted or sound. Known history: cast-order-as-hard-prune once deleted 37 reachable
  stompy wins (verdict later VOID — measured without TOP_RESOLVE; needs re-measurement); the
  Irencrag "needs the float" gate ADOPTED as a prune (was measured, but at ship settings — its
  limit behaviour is unexamined). ACTION when picked up: enumerate every adopted prune in the
  searched part and classify sound / exempted / lossy-at-limit.
* **Condemnation (per-provider opt-ins).** Sound-condemnation machinery exists; the opted-in
  decks' filters should be re-checked against the invariant (the AL/Kitty opt-ins measured
  inert; hinata deliberately opted out).
* **Discard/cleanup bucket rules, mana-tap orderings inside searched lines.** Per the USER,
  the recoverability test applies to these too: where they only ORDER exploration they are
  recoverable; where they DROP options they are not. Not yet classified.

## The verification instrument (when commissioned)

The "infinite-budget test", operationalized: a small fixed game sample per deck run at shipped
settings vs unlimited budget + large W vs unlimited budget + large W + suspect-lever-off. Any
line reachable in the third arm but not the second is a lossy element in the searched structure.
`g_fs_trunc_events == 0` in the unlimited arms is the precondition that makes the comparison
meaningful. This is cheap per game and does not need the full suite — it is a reachability
check, not a quality measurement.

## Context

`docs/design/bp-greedy-continuation-deletion.md` (the adoption + rulings),
`no-lossy-truncation` USER bar (2026-08-14), `heuristics-wired-as-prunes` doctrine,
`docs/design/post-breakpoint-search.md` (waves), `in-tree-greedy-reachability-hole` (the
measured pre-node unreachability this arc closed).
