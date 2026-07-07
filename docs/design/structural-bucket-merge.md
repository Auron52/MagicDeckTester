# Deferred: structural (byte-identical-definition) bucket merge

**Status: PARKED (2026-07-07). Implement only when the trigger below fires.**

## The idea

The exhaustive-keep bucketing (`DiscoverEquivalence`) clusters cards by their goldfish win-turn
**signature** with a small distance `threshold` (single-linkage). Two cards whose engine-relevant
**CardDefinition** is identical (same template + params + cost + types + P/T; name/oracle-text aside)
are *provably* the same card to the engine, so they could be merged **by construction**, independent of
any probe signature — a stronger, non-statistical guarantee (cf. the `goldfish_inert` by-construction
merge already in `DiscoverEquivalence`).

## Why it's parked (not implemented now)

1. **Redundant with the signature in practice.** Byte-identical cards produce byte-identical signatures
   (distance exactly 0), so the current threshold already merges every such pair. Its only *independent*
   value is robustness against a name/ID-dependent search tiebreak splitting an identical pair past
   threshold (the same failure mode that hid Distorting Wake — but that one was `goldfish_inert`, not
   byte-identical).
2. **No benefit to the decks in play.** Byte-identical groups in the card set (scan 2026-07-07, 102
   cards → 4 groups): {Muscle Sliver, Predatory Sliver}, {Cavern of Souls, Secluded Courtyard,
   Unclaimed Territory}, {Dauntless Bodyguard, Venerable Knight}, {Memory Lapse, Remand}. The signature
   already merges the first three where they appear (e.g. slivers); Memory Lapse/Remand is in Hinata but
   the inert rule already merges it. So Hinata stays K=20 either way.
3. **Do NOT implement via `CardParams::operator==`.** That struct is 30-plus fields and grows over time;
   a hand-written equality is a maintenance hazard (forget a new field → silent *over*-merge = fidelity
   bug). The robust route is a **definition hash of the raw JSON** (everything except name/oracle-text),
   computed at card-load and exposed per card, then union cards with equal hashes. Low-maintenance, no
   struct `==` to track. (A Python reference for the exact grouping is the scan used above.)

## Trigger to implement

Implement when a deck being profiled has a **byte-identical pair that the discovery puts in DIFFERENT
buckets** — i.e. the signature clustering fails to merge a provably-identical pair (analogous to the
Distorting Wake near-miss, but for byte-identical cards). Detect cheaply by comparing the JSON-hash
groups against the discovery's reported classes for that deck; if a JSON-identical group spans >1 class,
the structural merge is warranted. Until that happens, the `goldfish_inert` by-construction merge plus
the signature threshold cover the observed cases.

## Related

- `goldfish_inert` by-construction merge — the *implemented* sibling; catches cards that differ in
  cost/type but are equivalent because those fields are never used (never cast). Byte-identical merge
  would NOT catch inert cards (they are not byte-identical), and vice-versa — the two rules are
  complementary, neither subsumes the other.
- An alternative general-robustness lever (covers both cases without structural comparison): a more
  outlier-tolerant signature distance (median / trimmed-mean |Δ| instead of mean) so a few
  heuristic-perturbed probes can't inflate a real equivalence past threshold. Also parked.
