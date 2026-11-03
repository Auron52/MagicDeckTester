# The keep-table cache grew without bound (fixed 2026-11-03)

## What happened

On 2026-10-22 the container's `~/.cache/mtg/keeptable/` held **558 GB in 1,828 files**, about half of the 2 TB
host disk once the Docker VHDX had grown to hold it. A GC kept the 69 tables that the live checkouts' keep models
map to (21 GB) and deleted the other 1,759 (578 GB). The fix was deferred.

On 2026-11-03 at about 02:05 UTC it happened again, worse: the container's 1 TB disk (shared by `/`, `~/.cache`
and the build volume) reached 100% and runs died with ENOSPC. 800 GB of it was 2,663 `.keeptable` files. It was
cleared by hand (tables whose header `src_path` no longer existed, and tables of worktrees with no live
process; `logs/kt_gc/` in the primary checkout holds the header parse), and the fix below was built.

## Why it accumulated

Format v1 named a table after the **canonical path** of the keep model it decodes. Every distinct path got its own
table:

* each worktree's `decks/<Deck>/<Deck>.keepmodel.exhaustive.profile.json.gz` (~60 worktrees, ~13 GB each);
* each archived list (`decks/<Deck>/v1-*/`);
* each scratch copy a harness makes (deck_compare apparatus dirs, valueleaf / mullgen staging).

So the cache held N copies of the same table, at up to 3.2 GB each (FiveColour), and nothing ever deleted a table.

## The fix (`src/ai/KeepTable.h`, `CachedExhaustiveKeep` in `src/ai/MulliganProfileIO.h`)

All three options from the deferral note, together:

1. **Keyed by content.** A table is `<sidecar filename>-<fnv1a64 of the sidecar's bytes>.keeptable` (format v2),
   validated by the content hash and size in its header. Identical keep models in N worktrees share one table.
   Hashing is paid once per `(canonical path, size, mtime)`: a ~60-byte `<sidecar filename>-<fnv1a64 of the
   path>.keepref` beside the tables remembers the answer, so a launch costs one extra small read. The sidecars
   are at most ~60 MB, and byte-wise FNV-1a runs faster than the 9p checkout reads them.
2. **Mark use.** Each process that opens or builds a table sets the table's mtime to now. mtime is the cache's
   "last used" stamp; atime is unreliable under relatime. The header holds the source's fingerprint, and `Open`
   never reads the table's own times, so stamping cannot invalidate a table. A unit test covers this.
3. **Sweep** (`keeptable::Collect`), after every build and otherwise at most once an hour across all processes
   (the `.gc-stamp` file's mtime):
   * a ref whose sidecar is gone or has changed size/mtime is removed. The remaining refs make up the live hash set;
   * a `*.tmp` build partial older than a day is removed. A live build rewrites its temp continuously;
   * a v2 table that no live ref names and that has been idle for more than an hour is removed. The grace
     period covers a process that writes its ref and then its table between the sweep's two passes. A legacy
     v1 table whose header `src_path` is gone is also removed: v1 is path-keyed, so nothing can validate it again;
   * if what remains is over the cap, the least-recently-used tables are removed until it fits. A table used
     in the last 10 minutes, or one this process has open, is never evicted.
   Every removal is best-effort. On Windows (the play viewer) an open file cannot be deleted, and another
   process may race the sweep. Removing a table another process is reading is harmless on POSIX, because its
   descriptor keeps the inode alive. In every case the worst outcome is a later rebuild, never a wrong policy,
   because `Open` validates every table it reads.

**The cap.** `MTG_KEEP_TABLE_CACHE_GB` is a whole number of GB; 0 means unbounded and turns off LRU eviction (orphan,
stale-ref and stale-temp removal stay on). The **default is 64**. The content-keyed working set is ~13 GB per
generation of the whole deck set, so 64 GB holds ~5 distinct generations, which covers every branch a busy machine
plausibly plays in a day, and is ~6% of this 1 TB disk. The sweep also clamps the cap to a quarter of
(cache + free space). A smaller disk (the viewer's Windows machine), or a disk filling for other reasons, therefore
shrinks the cache instead of running out of space.

The cap is **soft**. Tables in active use are never evicted for it, because on POSIX unlinking an open file frees
nothing and the next launch would only rebuild the table. A working set larger than the cap stays over the cap
rather than thrashing.

## Coexistence with older binaries

v1 and v2 tables have different names (path hash vs content hash) and versions, so an older binary (another
checkout that has not pulled this) and a newer one never read each other's tables. The newer binary's sweep does
see v1 tables. It removes those whose source path is gone, and otherwise ages them by their mtime, which an older
binary never refreshes, so they are the first to go under the cap. An older binary that loses its table to the
sweep rebuilds it on its next launch.

`scripts/critter_order_ab/{make_ab,kt_check}.py` compute the **v1** table name to find a checkout with a warm
cache. Under v2 every checkout with the same sidecar bytes shares the table, so their root choice no longer
matters. They fall back to the worktree root, which is harmless.
