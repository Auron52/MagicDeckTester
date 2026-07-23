# CheckLine rejects a payable Medallion+ritual line (Apex of Power) — declared-order affordability

2026-07-21. User artifact: `logs/play/rejections/Dragonstorm_cod_s7_gi0_t3.json` — a viewer line
rejected as **illegal / "can't pay {7}{R}{R}{R} for Apex of Power"** that is actually **payable**.

## The line (seed 7, turn 3)
Board: Mountain ({R}) + Mercadian Bazaar (1 storage counter → 1 {C}) = **2 base mana**.
Declared cast order:
`Rite of Flame, Ruby Medallion, Pyretic Ritual, Seething Song, Desperate Ritual, Desperate Ritual, Apex of Power`.

Hand-traced with Ruby Medallion (reduces_spell_color=R → red spells cost {1} less **once it is in play**):
| step | cast | cost (w/ Medallion) | pool after |
|---|---|---|---|
| 1 | Rite of Flame {R} (pre-Medallion, no disc) | 1 | 3 (+2 float) |
| 2 | Ruby Medallion {2} | 2 | 1 |
| 3 | Pyretic Ritual {1}{R}→{R} | 1 | 3 (+3) |
| 4 | Seething Song {2}{R}→{1}{R} | 2 | 6 (+5) |
| 5 | Desperate Ritual {1}{R}→{R} | 1 | 8 (+3) |
| 6 | Desperate Ritual →{R} | 1 | 10 (+3) |
| 7 | Apex of Power {7}{R}{R}{R}→{6}{R}{R}{R} | 9 | **1** ✓ |

**Payable** (pool 10, Apex costs 9). So the "illegal" verdict is wrong.

## Root cause
CheckLine step-2 affordability (`TurnSolver.cpp` ~L9153) is a **producers-first greedy**: it casts
rocks + mana rituals *first* (so a ritual's float is online for the rest), which is deliberately
**order-independent** ("doesn't penalise the human's click order"). But Ruby Medallion is **not** a
producer, so it is cast in phase 1 — **after** all the rituals. Its discount only helps red spells
cast *after* it, so casting it late means the rituals pay full price and the pool tops out ~6–8
instead of 10; Apex is then checked at the **undiscounted {7}{R}{R}{R}=10** and rejected.

The producers-first reorder **defeats a same-turn Medallion**. The `EffectiveCost` discount
(`TurnSolver.cpp:641-660`) only counts Medallions **already in play** — its own comment defers
same-turn Medallions to "ManaPruneBound's bail," which CheckLine's aggregate walk doesn't apply.

The optimal order is a specific interleave (Rite for mana → Medallion → discounted rituals → Apex) —
which the **user played correctly**. Neither "producers-first" nor "Medallion-first" finds it
(Medallion-first stalls: paying {2} for the Medallion from 2 base mana leaves nothing, and Rite has
no generic to discount).

## Fix (IMPLEMENTED — viewer-only, GT-neutral; CheckLine's sole caller is `--validate-line`)
Added a **declared-order-first** affordability pass to CheckLine (`TurnSolver.cpp`, just before the
producers-first greedy): walk the casts in the user's given order, tracking a ManaPool + the set of
same-turn `reduces_spell_color` permanents cast so far, and apply each cast's discount positionally
(generic −1 per matching in-line reducer). If the declared order is payable → `LegalNotEnumerated`.
If not, fall back to the existing producers-first greedy (current behavior) so a human who clicks in
a bad order still benefits.

**Verified:** the artifact line → `legal_not_enumerated` (was `illegal`); bare Apex (2 mana) and the
same chain WITHOUT Ruby Medallion both stay `illegal` (no false-accept); goldfish smoke byte-identical
(CheckLine is not in the goldfish path). This:
- fixes the false "illegal" (→ correctly `LegalNotEnumerated`, the bug-finding signal that the search
  missed a legal line — the enumerator offered 40+ plans, none casting Apex);
- realizes the deferred **"honor the user's declared cast order"** item (the user orders correctly),
  while keeping producers-first as a safety net;
- is strictly additive (only accepts more lines) and viewer-only, so it cannot move GT.

## Open question for the user
`LegalNotEnumerated` is a **soft-reject** in the viewer (index.html ~L1027) — it is *not executed*.
The user wants to actually PLAY this line. So beyond correct classification, either (a) the search
must ENUMERATE the Medallion+Apex line (a deeper enumerator fix — its BuildPool has the same
same-turn-Medallion blind spot), or (b) the viewer should EXECUTE a `LegalNotEnumerated` line the
affordability sim can pay for. Decide which before wiring execution.

## Side fix shipped this session
The viewer hardcoded `gameIndex:0`; corrected to **`seed-1`** (index.html) so each seed plays its
intended opponent creature pattern (`PopulateOpponentSpawns(state, game_index)`). game_index affects
only the OPPONENT, so it does NOT change this line's mana — the two issues are independent.
