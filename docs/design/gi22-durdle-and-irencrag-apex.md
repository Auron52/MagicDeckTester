# gi22-class durdles + the Irencrag/Apex ordering rules (OPEN — fixable, not value-leaf)

2026-07-21. After the cast-ordering search (`ccec4e8`) recovered 6 of the 12 fea3a2c durdles, **9 persist**
(gi22, gi74, gi126, gi136, gi110, gi137, gi188, gi240, gi281). I first mis-filed these as a value-leaf
follow-up. **The user corrected this: they are NOT budget and ARE fixable without the value-leaf.**

## What gi22 actually is (faithful d3 run, seed 2024 game-index 22)

- **NOT budget:** win stays T6 across a **4096× budget sweep** (budget 10 → 40960, all 6.0). A
  budget-starved search would move.
- **NOT AccelPrefix:** `MTG_UNPRUNE=accelprefix` does not change it.
- Kept hand (mulliganed to 5): **Mountain, Mercadian Bazaar, 2× Lotus Bloom, Dragonlord Kolaghan,
  Irencrag Feat** (+ draws Scourge of Valkas, Dragonstorm). Mana comes from **Lotus Bloom (suspend)**,
  not a ritual chain. Only 2 real lands all game (Bazaar + Mountain, 1 red source).
- NEW does nothing T1–T5, combos **T6** (Dragonstorm → Lathliss + Scourge + Karrthus + tokens). explain_game
  says OLD (pre-fea3a2c) won this hand at **T4**. So the fea3a2c-era search stopped finding a line it used
  to find — same hand, not budget.
- The mulligan's *first thrown-away* hand had **Apex of Power + Irencrag Feat** together — the trap below.

## The user's ordering rules (authoritative; extends dragonstorm-cast-order-search.md)

1. **Rituals** (Rite, Pyretic, Desperate, Seething) chain into each other — **cheapest-first** goes off;
   never search their relative order.
2. **Irencrag Feat** — allowed in the ordering, but **only immediately before Dragonstorm, or as the
   second-to-last cast of the turn**. Its "you may cast only one more spell this turn" (`max_casts_after=1`)
   means exactly one cast may follow it. **Irencrag before a closing Dragon** is *usually* low value
   (dragons are relatively low impact) but must **not be cut off entirely** — keep it as a generated,
   search-decided option, just not a preferred one.
3. **Irencrag before Apex of Power is a TRAP — never generate it** (excluding it is "a no-brainer").
   Apex adds 10 mana of one colour AND exiles 7 cards to cast this turn; after Irencrag you may cast only
   ONE more spell (Apex itself), so Apex's 10 mana and its 7 exiled spells are **unusable/wasted**.
   **But the reverse — Apex → Irencrag → Dragonstorm — is a GOOD line and must be generated:** cast Apex
   first (it produces the mana and exiles the cards), *then* Irencrag, *then* Dragonstorm as the single
   permitted closing spell. So Apex is an **enabler that sits BEFORE Irencrag**, not a closer; Dragonstorm
   / a closing Dragon is the **closer AFTER Irencrag**.

   Implemented in `DragonstormCastOrderings` (`TurnSolver.cpp`) by splitting the old single "finisher"
   bucket into `apex` (`impulse_exile>0`, inserted **before** Irencrag) and `closer`
   (`tutor_to_battlefield || IsCreature()`, inserted **after** Irencrag, last). This makes
   `... apex, irencrag, closer` the base chain: Irencrag→Apex is never emitted, Apex→Irencrag→Dragonstorm
   always is, and Irencrag→Dragon remains an offered (low-priority) option.
4. **Ruby Medallion** — the one genuinely searched position: **as early as it can be paid** (earlier
   discounts more red rituals); move it later or drop it from the line only if necessary (the subset
   enumerator supplies the no-Medallion lines).
5. **Multiple Desperate Ritual vs Seething Song** — **splice-after Seething preferred** (that line is
   superior if it works — you splice the Desperates once Seething's mana is up); individual-before is the
   fallback. Needs search.

## Open hypothesis / next step (post-compaction)

Rule 3 is now **enforced** — `DragonstormCastOrderings` splits Apex (enabler, before Irencrag) from the
closer (Dragonstorm / Dragon, after Irencrag), so Irencrag→Apex is never emitted and Apex→Irencrag→
Dragonstorm always is. Remaining:
- gi22 itself: with Lotus Bloom mana + Irencrag, find why the d3 search no longer prices the earlier
  (T4) go-off it used to. Since it is not budget/AccelPrefix/ordering-cap, suspect the enumeration of the
  Lotus-Bloom-funded Irencrag→Dragonstorm subset, or a mis-scoring of that line under the fea3a2c order.
  Reproduce faithfully via `--log-dir` (NOT `--game-log-dir`, which writes nothing); the deterministic
  virtual budget makes single CLI games faithful (mulligan included), but OLD-vs-NEW binaries can keep
  different hands, so compare via `test/explain_game.py`.

Net context: the 9 persists are heavily outweighed (851 faster / 17 slower) and the GT is already
rebaselined (`49e3ce8`), so this is a quality follow-up, not a blocker.
