# `Card::m_keyword_mask` has overflowed its 32-bit width (latent UB, currently benign)

**Found:** 2026-09-25, incidentally, during the WhiteKnights deck analysis (a proposed
`Keyword::BattleCry` would have been the 36th enumerator).
**Status:** NOT a live bug. Undefined behaviour that currently resolves harmlessly on this
toolchain. **Do not fix as a drive-by** — see "Why this is not urgent, and why it is not nothing".

## The defect

`src/core/Card.h`:

```cpp
uint32_t m_keyword_mask = 0;                     // line 334
// The enum value's ordinal is its bit index; every enum above has < 32 values.
static constexpr uint32_t Bit(Keyword k) { return 1u << static_cast<int>(k); }   // line 368
void AddKeyword(Keyword k)       { m_keyword_mask |= Bit(k); }
bool HasKeyword(Keyword k) const { return (m_keyword_mask & Bit(k)) != 0; }
```

**The comment's premise is false.** `enum class Keyword` now holds **35** values:

| index | enumerator | cards.json users |
|---|---|---|
| 0 | `Haste` | many |
| … | … | |
| 31 | `Investigate` | Conservatory, Kitchen (last SAFE index) |
| **32** | **`Persist`** | **Kitchen Finks, Murderous Redcap** |
| **33** | **`Evoke`** | **Reveillark** |
| **34** | **`Convoke`** | **Chord of Calling** |

`1u << 32` on a 32-bit type is undefined behaviour (C++ [expr.shift]/1).

## What actually happens today — measured, not assumed

The UB resolves **two different ways in the same binary**, and which one you get depends on
whether the shift count is a compile-time constant (`logs/wk_probe/bit3.cpp`, g++ -O3):

```
constant-folded  Bit(Persist) = 0x00000000     <- no bit set: the tag is silently DROPPED
runtime-shifted  Bit(idx 32)  = 0x00000001     <- x86 masks the count mod 32: ALIASES Haste
```

**The engine takes the constant-folded path**, so today the three over-width keywords set *no bit
at all*. `HasKeyword(Persist)` is permanently `false` — which happens to match exactly what those
enumerators' own comments say they are for ("an INERT keyword-ability tag … no engine code reads
this enumerator"). The mechanics themselves are param-modelled (`CardParams::persist`,
`evoke_cost`, `convoke`) and are completely unaffected.

### Proof that it is benign today (Melira Pod, which runs both Persist cards)

`scripts/wk_keyword_overflow_probe.sh` + `scripts/wk_keyword_overflow_control.sh`, three arms over
one pooled batch each, comparing **per-game play digests** (not just win turns):

| comparison | melira d0 (1000 g) | melira d3 (200 g) | reading |
|---|---|---|---|
| A (shipped, `["Persist"]`) vs B (`[]`) | **0 differ** | **0 differ** | the Persist tag sets nothing |
| A (`["Persist"]`) vs C (`["Haste"]`) | 923 differ | 193 differ | Persist does NOT behave as Haste |
| B (`[]`) vs C (`["Haste"]`) | 923 differ | 193 differ | real haste *would* change this deck a lot |
| B vs sanity (Ignoble Hierarch keywords stripped) | 432 differ | 79 differ | control: `--cards-json` IS live |

The A-vs-C and B-vs-C rows are the decisive ones: if Persist aliased onto Haste, A and C would be
identical. They differ in 92% of games, and A instead matches B exactly.

**The sanity arm matters.** Without it, "0 differ" is indistinguishable from a probe that never
moved the lever (the *digest-equality-can-mean-BROKEN* trap).

## Why this is not urgent, and why it is not nothing

It is **UB**, so the current benign folding is not guaranteed. The hazard is real in three ways:

1. **A refactor flips it.** Any change that stops the constant from reaching `Bit()` at compile
   time — an un-inlined dispatch, a `Keyword` variable, a table-driven loader — silently switches
   the three tags from *dropped* to *aliasing bits 0/1/2* (`Haste`/`Flying`/`Trample`). Kitchen
   Finks and Murderous Redcap gaining haste would change Melira Pod's play in ~92% of games, and
   Melira Pod is a shipped deck with an adopted mulligan profile.
2. **MSVC may already differ.** This repo builds on windows-latest and has a **determinism-parity**
   job asserting Linux and Windows agree for the same seed. UB is exactly where two compilers are
   free to disagree. Not yet checked.
3. **The next keyword added is the 36th**, which would alias `Deathtouch`. The failure is silent
   in both directions — there is no diagnostic.

## The fix, when someone schedules it

Widen the storage and the accessor to 64-bit:

```cpp
uint64_t m_keyword_mask = 0;
static constexpr uint64_t Bit(Keyword k) { return 1ull << static_cast<int>(k); }
```

and add a `static_assert` that the enum's count is under the width, so the next overflow is a
build error rather than silence. Note `Bit()` is overloaded for `CardType`/`Supertype`/`Color`
too — those enums are still small, so only the `Keyword` overload's return type changes.

**This is GT-moving and must be measured, not assumed.** Widening makes `HasKeyword(Persist)`
start returning `true`. Nothing reads it today, so the expectation is byte-identical play — but
that expectation has to be *proved* with a digest A/B across the suite (and specifically on Melira
Pod / Reveillark's deck), because "nothing reads it" is the kind of claim this very investigation
showed can be wrong. Do it as its own change with its own measurement, not folded into a deck
analysis.

## Consequence for new cards until then

**Do not add a new `Keyword` enumerator.** A 36th value aliases `Deathtouch` under the runtime
path and is dropped under the folded one — neither is acceptable. Model the mechanic structurally
via `CardParams` (the established idiom for all 20+ inert tags) and leave `keywords: []` in
cards.json, adding the keyword name to `MODELED_ELSEWHERE_KEYWORDS` in
`scripts/audit_card_fields.py` so the field audit still reality-checks the rest of the entry.

This is what **battle cry** does in the WhiteKnights work (`battle_cry_power` + `ApplyBattleCry`);
see `docs/design/analysis-WhiteKnights.md`.

## Correction to the record

This defect was first reported to me (during the WhiteKnights card fan-out) as a **live** bug:
"Kitchen Finks and Murderous Redcap currently read as hasty in Melira Pod." I confirmed the bit
arithmetic in isolation and repeated that claim before testing the engine. **That claim is wrong**
— the isolated probe exercised the runtime-shift path, the engine uses the constant-folded path.
The lesson is the one already in this repo's memory: an isolated probe proves the arithmetic, not
the behaviour; only running the engine settles what the engine does.
