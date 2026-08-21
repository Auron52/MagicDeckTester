# Width-knob review queue (Rule 0c inventory, deferred)

**Status: DEFERRED — awaiting USER review, knob by knob. Do NOT change any of these
unilaterally; several are measured adoptions.** Recorded 2026-08-21 during the fetch-fan
clairvoyance arc, when the USER's standing rule landed: *a heuristic returns ONE option by
default; silent top-n widths are forbidden; any widening is a user-review gate and must be
confidence-conditional per state* (encoded in the heuristic-optimization skill, Rule 0c).
The fetch fan was the origin case: its top-2/uncapped "wins" were proven 100% draw-order
clairvoyance by the MTG_SHUFFLE_SALT_SEARCH decouple ensemble, and top-entry-only was
adopted (0a59457) with GT deliberately worse.

This file is the inventory of the remaining engine/provider widths > 1 that predate the
rule. Each needs either (a) a user review that keeps it with a stated justification, or
(b) a measurement — **including the decouple ensemble wherever the choice can read future
draws (tutor/scry/dig/ponder-class)**, because coupled held-out provably cannot distinguish
real strategy from clairvoyance on that class.

| knob | default | notes |
|---|---|---|
| `TutorSearchWidth` (provider virtual) | 6 (Goblins 6 by holdout, antilife 2 = exhaustive, Hinata keeps 6 — w2 FAILED holdout and was no cheaper) | The best-vetted of the set, but every measurement was COUPLED; a decouple re-audit is the open item. Tutor targets are the textbook clairvoyance channel. |
| `MTG_BP_SEARCH` | 2 | Measured bp-waves adoption; width is the searched breakpoint-continuation fan. Review = confirm the measurement stands under Rule 0c, not re-measure by default. |
| `MTG_SCRY_WIDTH` | 2 | Land-ETB scry/surveil disposition fan. Library-order-adjacent → decouple ensemble applies. |
| `MTG_ETBDIG_WIDTH` | 3 | ETB-dig pick fan (Acclaimed Contender class). Library-order-adjacent. |
| `MTG_PONDER_ORDER_WIDTH` | 4 | Ponder reorder fan. Library-order class. |
| `MTG_LACKEY_WIDTH` | 2 | Lackey combat-cheat put fan (plus MTG_LACKEY_RANK orderings). Board-facing, not library-facing — plain measurement suffices. |

Perf context (why this is not urgent as a *cost* item): Hinata's w2-vs-w6 tutor probe was
no cheaper (68s vs 69s overnight makespan), so these are quality/integrity reviews more
than wall-clock levers. The recorded fetch-doctrine confidence rule (return the tied set
when ALL ranking keys tie at the top — else one) is a candidate template for
confidence-conditional widths, unproposed and unmeasured.

Related perf backlog (separate, from the 2026-08-21 assessment, all in their own docs):
GameState deep-copy/allocator churn (engine-cost-profile-2026-08-16.md — biggest honest
engine-wide item, needs its own session + deterministic-counter re-profile first);
sound-condemnation upgrade ("stamp only declined-BY-VALUATION") for the residual
MTG_5C_PHASE play cost (re-measure post fetch-top-1 before investing); cleanup-discard
top-1 fast path (~1.5% of FiveColour gen after correction, mechanical);
MTG_ENUM_MEMO_CAP raise REFUTED 2026-08-21 (fivecolour-gen-leaf-cost-wallclock.md).
