# Mid-phase ordering audit (2026-08-15)

**USER directive:** "we should look at anything that was handling mid-phase ordering to ensure
we don't break that" — in the context of the main-phase-classification collapse
(`main-phase-classification.md`, `single-consideration.md`), which moves most casts into ONE
post-combat enumeration. Every ordering rule below was written when the second main held 1-2
finishers; the collapse makes the m2 set large and heterogeneous, so each rule's small-set
assumption is now load-bearing. This doc is the ranked inventory (found by a full sweep of
TurnSolver / AIEngine / DecisionProviders / ManaPayment), with status.

The repeating theme (the classify arc's ledger): **the m1/m2 phase boundary secretly carried
semantics** — prowess feeding (pump category, per USER), enabler-first resolution, attack/cast
mana allocation order, fetch-target selection informed by same-phase casts. Each collapse
defect so far has been one of these surfacing.

## Fixed

* **Executor breakpoint continuation cast order (item 2, LOCKSTEP BUG — fixed 2026-08-15).**
  `AIEngine.cpp resolve_draw_breakpoint` cast a continuation's hand casts in RAW plan order
  while the rollout's `apply_plan_actions` applied the full canonical reorder — the executor
  could realise a different sequence than the search scored. Masked while continuations were
  near-singletons; real on staging decks at d0 (fallback re-solves): fixing it moved hinata d0
  7.0160→7.0090 and dragonstorm d0 5.4300→5.3990 (19 faster / 4 slower / 55 play-changed;
  searched depths byte-identical). Fix mirrors the main-loop branch pair: searched_order
  pinned; opaque set → enablers first (CastOrderRank-stable) then plan order; clean set →
  stable CastOrderLess.
* **Fetch-target cap under the collapse (routes table)** — already uncapped when
  `MainPhaseFilterActive` (commit a4071a6).

## Open — ranked, most-at-risk first (verbatim findings condensed)

1. **`OrderingOpaque` blanket** (`ManaPayment.cpp:572`; consumed TurnSolver ~10338, AIEngine
   ~2730): ANY draw/staging/impulse card in a set disables ALL rank-based ordering for the
   whole set (ritual tier, reducer, restrictor, self-damage-last, prowess-early all go dark);
   plan order ≈ hand-index order. Written for "one draw spell + leftovers"; a collapsed m2 set
   is routinely opaque. The ordering search that supposedly "owns" it is default-off.
2. **Spectacle hoist opaque-branch-only** (TurnSolver ~10369, AIEngine ~2767): a Spectacle
   card without a staging/draw rider in a CLEAN collapsed set silently loses the sac-burn
   hoist. Its own comment scopes it to "the 2-card {burn, Light Up} plans" — the assumption
   the collapse invalidates.
3. **Enabler-first depends on provider rank-0** on clean sets: a provider implementing
   `CastEnablerFirst` without a matching `CastOrderRank` 0 override gets NO enabler-first on a
   clean set. The collapse-specific emissions (in-hand alt payloads) make this legality-
   bearing, not just quality-bearing.
4. **Subset scoring is phase-blind** (`Solve::consider` / `eval_and_push`): the
   `projected_atk` prowess/haste terms are pre-combat notions computed identically in the
   post-combat enumeration that now prices the whole hand.
5. **Sequenced ritual credit mode asymmetry** (default mode 1 = Solve-only): the greedy m2
   uses the tight sequenced affordability model while FSLineTail's m2 branch list uses the
   optimistic simultaneous one — the two m2 paths disagree about the same large set.
6. **Cantrip-first rewrite dominates opaque sets** (`ApplyCantripFirstOrder`): `[rest]` keeps
   candidate order (not rank order); in a collapsed set `[rest]` holds creatures, removal,
   self-damage sources.
7. **`BatchPrepayMainCasts` declines more on mixed sets** (any producer / {X} / discount /
   <2 casts / non-creature mix) → the phase falls back to per-cast greedy tapping, where
   order decides which sources are spent.
8. **`ManaUnlockColorReserve` over-reserves on big sets** (reserve test `makers <= need[c]`
   fires more as `need[]` grows); `fire_unlock()` not called after spectacle-hoisted sac
   casts or the trailing sac loop.
9. **Trailing ability pass has no internal order** (equip/put/sac chain in plan order): the
   set-level validity gates argue "a legal sequential order exists" — nothing realises it.
   Multiple equips/puts in one collapsed phase widen the gap.
10. **`Plan::searched_order` is EnumeratePlans-only** and bails at >120 permutations (k>=6),
    exactly the size a collapsed set reaches; the greedy m2 path can never produce it.
11. **Ponder/cast_reorder axis runs in m2 un-gated** while the Lackey axis is m1-only — the
    known disposition-flip residual class (main-phase-classification.md).
12. Latent: executor opaque-branch `alt_cost` cast is not `sacrifice_land`-guarded (double
    cast if a sac-land alt card ever exists); `CanonicalNonSacCastOrder` (viewer diff) ignores
    `OrderingOpaque`; `MTG_ORDER_TRACE` is m1-only (blind to where the casts now live).

## m1-only paths the collapse routes casts away from

Land drop greedy fallback, breakpoint land plays (`play_breakpoint_land`,
`play_drawn_flood_keep_land`), dig-when-stuck, `TapDripLandsIfUseful`, animate/tap-tokens,
Lackey axis — all `is_pre_combat`-gated. Anything the classifier defers loses access to these
in-phase; `MTG_MAIN2_DROP` covers only the land drop.

## Doctrine consequences (measured 2026-08-15)

* **`MTG_DOUBT_MAIN2` (parked, default off):** the USER's one-pool placement rule (explicit
  attack-helping classes stay Main1; tutors classify as card-flow Both; residual doubt →
  Main2). Structurally right (it removes the cross-phase mana partition class, antilife
  gi=58), but measured AS-IS it regresses antilife 4.3267→4.3933 — the flip feeds MORE mass
  into the m2 set before the hardening above exists. Re-measure after items 1/4/5 land.
  **Dissected (24 worse / 4 better on antilife d3; the 4 better INCLUDE the partition-class
  games gi=87/118/286, so the design intent works):** the visually-dominant pathology is
  gi=1's committed line SKIPPING the land drop on T1 AND T2 and pitching Birds then Swords
  at cleanup (defer-land + discard ranked above play-Mire).
* **The land-skip pathology ROOT-CAUSED (2026-08-15) — it is NOT a ranking bug, and the
  "defer wins only when strictly better" fix direction is REFUTED.** Full FSLine + rollout
  tracing on gi=1 (`--seed 2003 --game-index 1`) showed: every T1 land option is a
  FETCH, whose in-search crack RESHUFFLES the library (`ShuffleAfterSearch`, CRN-keyed),
  while the defer/do-nothing arm keeps the NATURAL order. Commit-the-line's evaluation is
  deliberately clairvoyant and lockstep with the executor, so each arm's rollout REALIZES
  its own (known) future order — and when the natural order is hot (gi=1: Swords T2,
  Remedy T3), every no-shuffle line systematically beats every crack line (heur d1 tails:
  defer=5 < Mire=6 < Marsh=8). With doubt-on emptying the m1 cast set, this shuffle-order
  difference is the ONLY signal left, so total passivity wins strictly — no tie-break or
  land-credit can fix an arm that genuinely scores better. Verified both ways:
  `MTG_NO_SEARCH_SHUFFLE=1` (order invariant to cracking) flips gi=1 to lands played
  T1-T3, win at 4. The value model is NOT the blocker (`MTG_VALUE_MODEL=0` still skips).
  **But the aggregate doubt cost is NOT this class:** the 4-arm 300g A/B (stack±doubt ×
  ±NO_SEARCH_SHUFFLE, seed 2002 d3) gives 4.3267/4.3933 (shuffle) vs 4.7200/4.7933
  (noshuffle) — the doubt gap is ~+0.07 in BOTH universes (27 worse/6 better even with
  order invariance), and noshuffle itself is a −0.4 game-model regression (it reverts the
  rules-correct search shuffle, changing the executor too — not a candidate). So: the
  doubt flip's residual cost lives in the m2-emission semantics this doc catalogues
  (post-combat pump emission needs an attacker, alt-payload gates, item 1/3/4/5), spread
  thin across games; the flip stays PARKED until that hardening lands, and the land-skip
  *shape* of the failures is an artifact of clairvoyant crack-vs-no-crack comparison at
  value-flat nodes — a decision-theory property of the engine, only addressable at the
  architecture level (honest/common-future evaluation would break commit-the-line's
  replay contract; noshuffle is measured worse).
* The explicit attack-helping classification (PumpSpell/Lord/Haste templates, equipment,
  haste-granters, firebreathing, board-scaling, pump params) is IN (behaviour-neutral today —
  it encodes what doubt-Main1 gave those cards implicitly) and is the precondition for any
  future doubt deferral.
