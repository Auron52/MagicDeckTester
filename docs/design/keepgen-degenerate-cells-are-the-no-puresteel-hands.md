# 1.5% of keep-gen rollouts burn 75% of the machine, and they are the hands a human insta-mulligans

**Status: MEASURED 2026-10-09 on the kitty v3 keep generation, NOT FIXED.** No engine change was
made: a generation was in flight and its freeze keys on `HEAD:src`, so touching `src/` would have
invalidated a multi-hour run. This is the write-up so the finding survives the run.

## The measurement

Mid-run state of the v3 exhaustive keep generation (`recipe fast`, `R=30`, `K=15`, `depth=1`,
`budget_ms=3`, `max_turns=8`), 8.1 h in:

| | |
|---|---|
| rollouts completed (`roll7 + rollsub`) | 369,152 |
| wall | 29,104 s on ~32 cores ⇒ ~259 core-hours |
| rollouts logged as SLOW (>30 s each) | **5,362** = 1.45% of all rollouts |
| core-hours inside those 5,362 | **194.7** = **75% of the machine** |

The single worst cell-side took **3,405,242 ms = 56.8 minutes for one rollout**, at `size7 draw r=0`.
Two more exceeded 35 minutes. 61 rollouts exceeded 15 minutes; 490 exceeded 5 minutes.

## What those cells are

Presence rate of each own-class card among the 5,362 slow records, against the rate the cell
enumeration implies (~33% for any single class in a size-7 multiset over 15 classes):

| class | slow-tail presence | enumeration baseline | read |
|---|---|---|---|
| `Puresteel Paladin` | **8 / 5,362 = 0.15%** | ~33% | **essentially absent** |
| `Sram, Senior Edificer` | **4,031 / 5,362 = 75%** | ~33% | **2.3x over** |
| `Remote Farm` | **3,284 / 5,362 = 61%** | ~33% | **1.8x over** |
| `Dwalin, Weaponmaster` | 0.39x vs slow-tail mean | — | under |
| `Sigarda's Aid` | 0.31x vs slow-tail mean | — | under |

**The degenerate cell is: Sram + Remote Farm, no Puresteel, no second body, no auto-equip.**

The mechanism is deployability, and the under-represented cards are the positive evidence for it
rather than a story told after the fact. Puresteel Paladin's metalcraft equips for free, so a hand
holding it converts an equipment pile into lethal and the rollout **ends**. Dwalin, Adelbert and
Sigarda's Aid each likewise resolve the board — a body to carry the pile, or an attach-on-ETB that
does not need the mana. A hand with none of them, holding Sram (`{1}{W}`, a draw engine that wants a
stream of casts) off `Remote Farm` (enters tapped with 2 depletion counters — **not** a Karoo bounce
land; it yields two mana total and then dies), can neither deploy nor die. It plays all 8 turns with a
wide equipment menu re-enumerated every turn. That is the 56.8-minute rollout.

So the cells eating three quarters of the box are precisely the ones whose correct label is "mulligan"
— and the generator is paying full exhaustive price to discover it.

## Two of the signals in the first cut were BUCKETING ARTIFACTS — retracted here on purpose

The first pass read `Plains` at 12/5,362 against a 65.4% hypergeometric baseline and called it the
land signal. **That was wrong, and the shape of the error is worth keeping.** The equivalence classes
are:

```
 0  [Accorder's Shield, Cathar's Shield, Kite Shield, Spidersilk Net]
 1  [Ancient Den, Plains]
 9  [Puresteel Paladin]      10 [Remote Farm]      14 [Sram, Senior Edificer]
```

`Plains` shares class 1 with `Ancient Den`, and the slow log prints the **class representative**, so
`Plains` *cannot* appear as a hand label — its absence carries no information at all. Same for
`Cathar's Shield` / `Kite Shield` / `Spidersilk Net`, all folded into class 0 behind
`Accorder's Shield`. Only cards in a **singleton class** support an inference, which is why
`Puresteel`, `Sram`, `Remote Farm`, `Dwalin` and `Sigarda's Aid` are the ones quoted above.

Second correction in the same vein: the hypergeometric baseline is also the wrong denominator. The
cell space is a **uniform enumeration over bucket multisets**, not a sample of random 7-card hands, so
a 4-of and a 12-of are weighted alike. The enumeration marginal (~33%) is the honest baseline. It does
not change the verdict — 0.15% against 33% is a 200x deficit — but quoting 65.4% would have been
quoting an artifact against the wrong reference.

## Why this is in scope rather than "the deck is just expensive"

CLAUDE.md's standing rule is to **collapse wasted search unconditionally** — *"Wasted work is wasted
work regardless of the situation"* — and its second, sharper reason applies exactly here: every
unnecessary branch is noise in the census that would locate the next defect. A tail where 1.45% of
rollouts hold 75% of the cost is the loudest possible census signal.

It also bears on the **3x cost rule** and on the v1-vs-v3 promotion that is still the user's call: v3's
measured generation cost is ~1.5x v2's on the half that has completed (floor+sub: 27,604 s vs
18,002 s, on 9% *fewer* cells and with PGO's 1.32x already credited, so ~2x like-for-like). If that
multiple is 75%-attributable to undeployable cells, it is a fixable cost and not a property of the
list.

## The constraint any fix must respect

The completeness invariant is in force: **a budget may re-price work, it may not make a line
unfindable.** So the shapes that are legitimate here are the ones that are provably inert at unlimited
budget:

* **A per-rollout stall price.** Charge decision units for a turn in which the active player cast
  nothing, attacked with nothing and has no creature. This is a *price*, read only when
  `budget != nullptr`, so it vanishes at an unlimited root exactly like
  `MTG_EQUIP_ROUTE_NODE_UNITS` does.
* **Admission order, not refusal.** If undeployable frames are to be deprioritised, stage them into a
  later wave so a small budget spends on high-yield frames first and a larger budget still reaches
  them. This is the one place a wave is *required* rather than forbidden — it is staging inside one
  search, not scheduling a pooled run.

What is **not** legitimate: a gate predicate that refuses these cells (a `size7 && !has_puresteel`
skip), or a hand-written mulligan rule that labels them without rolling them. Both would make the
label unfindable at any budget, and the keep table's entire value is that it is *measured*. The cell
must still be evaluated — it just must not cost 57 minutes.

**Which of these to build, and whether to spend the time at all, is the user's decision.** The
measurement is the input to it, not the decision.

Related: `docs/design/play-digest-gate-misses-rollout-only-heuristics.md` (the other defect this same
run surfaced), `docs/design/kitty-discard-buckets-are-stale-for-v3.md`,
`.claude/skills/mulligan-profile.md`, `src/analyzer/ExhaustiveKeep.cpp` (the monitor and the slow log
this was read off).
