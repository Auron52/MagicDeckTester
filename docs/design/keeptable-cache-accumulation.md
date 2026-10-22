# The keep-table cache grows without bound (deferred, 2026-10-22)

## What happened

On 2026-10-22 the container's `~/.cache/mtg/keeptable/` held **558 GB in 1,828 files**, about half of the 2 TB
host disk once the Docker VHDX had grown to hold it. A GC kept the 69 tables that the live checkouts' keep models
map to (21 GB) and deleted the other 1,759 (578 GB). The script is `build/scratch/keeptable_gc.py` in the primary
checkout; it is not committed.

## Why it accumulates

`KeepTableDir() / TableFileName(src_key)` (`src/ai/KeepTable.h`, `src/ai/MulliganProfileIO.h`) names a table after
the **canonical path** of the keep model it decodes. The comment says "a regeneration overwrites rather than
accumulates". That holds for one path. But every distinct path gets its own table:

* each worktree's `decks/<Deck>/<Deck>.keepmodel.exhaustive.profile.json.gz`;
* each archived list (`decks/<Deck>/v1-*/`);
* each scratch copy a harness makes (deck_compare apparatus dirs, valueleaf / mullgen staging).

So the cache holds N copies of the same table, one per path that ever loaded it, at up to 3.2 GB each
(FiveColour). Nothing ever deletes a table whose source path is gone.

The repo-side `<sidecar>.bincache` files this design replaced (6d3e911dc) were never cleaned up either. On
2026-10-22, 571 of them (143.7 GB) were still in old worktrees.

## Fix options (not built)

1. **Key by content, not path.** Name the table after a digest of the source (size + mtime + a hash of the
   gz header, or of the whole file). Identical keep models in N worktrees then share one table. This needs
   `Table::Open`'s `src_key` check to move from the path to the digest.
2. **Sweep orphans on build.** When `BuildKeepTable` writes a table, delete any table whose recorded source
   path no longer exists. This is cheap: one header read per file, once per build.
3. **Cap the directory** (LRU by atime, e.g. 100 GB). This is the backstop if 1 and 2 still leave too much.

Option 1 removes the multiplication; option 2 removes the orphans that 1 leaves (deleted worktrees).
