# The exhaustive keep table as an on-disk, seekable file

Status: **built 2026-09-15** (`src/ai/KeepTable.h`; loader in `src/ai/MulliganProfileIO.h`;
unit cover `test/unit/test_keep_table.cpp`). USER design: *"we want an uncompressed version of
the .gz on disk at least, even if we don't use a memory mapped file. The reason is that we need to
be able to seek."* Measurements at the end.

## The problem it removes

A deck's exhaustive keep/bottom table is consulted a handful of times per **game** — one lookup per
mulligan step plus one per bottoming decision — and never per search node. Yet the runtime held the
whole policy resident as two `std::map<std::vector<int>, ...>`: for FiveColour (1,977,898
compositions × K=27 buckets × 14 mull/play slots) that is **~5.3 GB resident**, on a 10.7 GB box
whose RSS watchdog (`MemBudget`, ¾ of RAM) trips at 8.02 GB. The `MemBudget` cap and cache budget
are already machine-relative; what was not machine-relative was the *artifact*: the policy's
footprint scales with the deck's model, and FiveColour's did not leave room for the games.

The `3bea3b72` streaming bincache fix bounded the load-time *transient* (the parsed-policy blob is
no longer materialised in RAM on either the write or the read side) and got the overnight tier from
an 8.14 GB abort to a 6.89 GB completion. But the resident policy itself was untouched — still the
real ceiling.

## The design

**Fixed-width records in ascending composition order, binary-searched on disk.**

```
[magic u32][version u32]                                          preamble, 8 B
[record 0][record 1] ... [record N-1]                             fixed width, strictly ascending
[header block]                                                    source fingerprint + policy scalars
[header_off u64][header_len u64][N u64][magic u32][version u32]   trailer, 32 B

record = int32 comp[K] | uint8 flags[F_keep] | uint32 bottom_mask | int32 rows[F_bot][K]
```

* **Seekable without a separate index.** Records are fixed width and sorted, so record *i* is at
  `8 + i·rec` and a lookup is a binary search: ~21 positioned reads of `4K` bytes for 2M records,
  then one read of the hit. At a few lookups per game that is free. The most recent hit is
  remembered, so the bottoming lookup that follows a keep lookup on the same hand costs no I/O.
* **Bit-exact with the maps by construction.** `bottom_mask` bit *i* set ⇔ row *i* present with
  length K. A clear bit is exactly the in-memory policy's "absent / empty (deferred) / non-K row",
  for which `DecideBottom` already returned false. The unit test builds a table from a policy and
  checks `Decide`/`DecideBottom` agree at **every key × mull × play/draw**, including the
  `min(mull, max_mull)` clamp, deferred rows, entries with no bottom table, and absent keys.
* **Self-validating length.** The header is written last (the record count is unknown until the
  streaming build finishes); the trailer's lengths make `Open` require
  `file size == 8 + N·rec + header + 32` exactly. That is the same length-exactness rule the old
  cache used: a short or torn file parses as *plausible* zeros (empty rows), which is a silently
  wrong policy rather than an error, so it must be rejected structurally.
* **Built by streaming, never via the maps.** `ParseDeckProfileJson` takes an optional entry
  sink; the loader hands each parsed entry straight to the table writer, so the one-time build
  peaks at the JSON text (~1.8 GB for FiveColour) instead of text + maps (~7.3 GB — which did not
  fit under the cap without a manual `MTG_RSS_CAP_GB` override; that is why the old cache had to be
  "warmed in place"). The writer enforces the shape (first entry fixes K / F_keep / F_bot; every
  later entry must match) and strict ascending order (the JSON emitters walk a `std::map`, so a
  shipped sidecar always satisfies it); any violation fails the build and the loader falls back
  to the in-memory policy — slow, large, never wrong.
* **Where it lives.** `MTG_KEEP_TABLE_DIR` if set, else the per-user cache dir
  (`$XDG_CACHE_HOME/mtg/keeptable`, `~/.cache/mtg/keeptable`, `%LOCALAPPDATA%\mtg\keeptable`),
  else the temp dir. **Never beside the sidecar**: this checkout is a 9p/drvfs share (whole-second
  mtimes, rename not atomic against an open fd, slow small reads). The file is named
  `<sidecar filename>-<fnv1a64(canonical path)>.keeptable` and validated by the source's
  `(canonical path, size, mtime)` stored in the header — a regenerated sidecar overwrites its own
  table, nothing accumulates, and a **symlinked** sidecar (deck_compare's apparatus dirs,
  keep_delta's link) shares the real file's table without the `.bincache`-symlink dance those
  scripts used to do.
* **Positioned reads, not mmap.** A mapping over a file another process is rebuilding turns a
  rejected cache into a SIGBUS, and mmap needs per-platform code (the play viewer runs `mtg` on
  Windows). One `std::ifstream` per table, mutex-serialised; portable and, at this call rate,
  unmeasurable.
* **It is a persistent cache, not a per-run temp.** A true temp would re-parse ~1.8 GB of JSON on
  every launch. The `<sidecar>.bincache` blob is gone entirely — it existed to skip the parse, and
  the table does that better (an `open()` plus a header read per launch instead of decoding a
  3.6 GB blob into maps).

## What did not change

* The generator, the merge tool, `SaveDeckProfile`, `ExhaustiveKeepToJsonObj` and every unit of
  the analyzer still work on the in-memory maps. Only **play** loads (`CachedExhaustiveKeep`,
  reached via `AttachExhaustiveSidecar` and `MTG_EXHAUSTIVE_PROFILE`) attach the table.
* The decision surface: `Decide` / `DecideBottom` / `empty()` / `buckets` / `name_to_bucket` /
  `bottoming_enabled`. Callers are untouched.
* The `analyzer`'s `LoadDeckProfile` of an exhaustive sidecar (the `H:comp` scorer reads
  `.buckets`) still parses into maps. It could open the table header instead; not done.

## Escape hatch

`MTG_KEEP_TABLE=0` (default ON) forces the legacy in-memory load. Use it only to A/B the backing;
it is the full 5 GB again.

## Measurements (2026-09-15, this 10.7 GB / 12-core box)

FiveColour, `--games 40 --seed 1001 --threads 4`, peak RSS from `ru_maxrss`
(`logs/keeptable/fc_measure.sh`):

| launch | wall | peak RSS | notes |
|---|---|---|---|
| old binary, warm `.bincache` | 50.1 s | **5.17 GB** | ~43 s of that is decoding the 3.59 GB blob into maps |
| new binary, **cold** (builds the table) | 70.4 s | 2.02 GB | 1,977,898 compositions → 3.09 GB table in 63 s; JSON text is the peak |
| new binary, warm | **7.5 s** | **0.14 GB** | open + header read; the games are the whole cost |

stdout byte-identical across all three (`avg (turns) 4.9000`). The old cold path (no bincache)
peaked at ~7.3 GB and could not run under the 8.02 GB cap without `MTG_RSS_CAP_GB`; the new cold
path fits with room to spare.

Table size per deck ≈ N × (4K + F + 4 + 4FK) bytes: FiveColour 3.09 GB (was a 3.59 GB bincache
*plus* 5.3 GB of maps). Lookup cost: ~21 positioned reads of 108 B + one 1638 B record, mutex-held,
per composition; a keep-then-bottom pair on the same hand pays once.

Fleet byte-identity: `test/regression.sh --smoke` against the committed GT — **79/80 identical**; the
80th (`critter2hg_smoke_d3_s1001`, 5.2000 → 4.7600) reproduces identically under the OLD binary and
under `MTG_KEEP_TABLE=0`, so it is a stale GT row (the EDF COMBO adoption `7e98f306` landed after the
critter rebaseline `7f7ad8a6`), not this change. Every deck built its table on first touch during
the smoke job load (19 tables, 0.1–12 s each; 7.4 GB under `~/.cache/mtg/keeptable`), no fallbacks.
