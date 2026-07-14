# Per-deck folder layout (deferred cleanup)

**Status:** deferred / not started. Do at a clean break (no in-flight profile
regen), then rebaseline nothing — this is a pure path move, byte-identical
artifacts. Motivation: `decks/` has grown cluttered — every deck carries a
decklist plus 3–5 generated artifacts, and there are stray experimental
variants (`*.r20/r80/r100`, `*.NEW.*`, `*.keepmodel.{regret,score,hybrid}.*`),
so a flat `decks/` is hard to scan. Goal: one folder per deck.

## Proposed layout — Option A (nest, keep filenames)

```
decks/<name>/
  <name>.cod | <name>.txt                          # decklist (entry point)
  <name>.profile.json                              # AI params (the ANCHOR)
  <name>.value.json                                # value-leaf model     (optional)
  <name>.eval.json                                 # hand-eval model      (optional)
  <name>.constraints.json                          # mulligan constraints (optional)
  <name>.keepmodel.exhaustive.profile.json.gz      # runtime keep/bottom policy
  <name>.keepmodel.exhaustive.raw.json.gz          # poolable gen sidecar (gzipped!)
```

Filenames are **unchanged** — only nested one level. This is deliberate:

- The engine resolves every sibling artifact **directory-relative** off the
  profile path — it strips the `.profile.json` suffix from `profile_path` and
  re-suffixes *in the same directory* (`MulliganProfileIO.h` constraints @286,
  keepmodel.exhaustive @629/634, eval @756, value @797). Because it keys off the
  profile's own parent dir + stem, moving all of a deck's files into
  `decks/<name>/` with the same stem **just works, no code change**.
- `scripts/analyze_deck.py` writes the profile with
  `deck_path.with_name(deck_path.stem + ".profile.json")` (@147) — also
  directory-relative, so pointing it at `decks/<name>/<name>.cod` writes the
  profile into the folder automatically. **No change.**

### Why NOT Option B (`decks/<name>/profile.json`, drop the redundant prefix)

Cleaner-looking, but it **breaks** the sibling resolver: filename `profile.json`
under a `.profile.json` suffix-strip yields an empty stem, so siblings become
dotfiles (`.value.json`, `.constraints.json`). Supporting it would mean
re-architecting the resolver to key on fixed role-names instead of stem+suffix —
much more invasive for no functional gain. Stick with Option A.

## Touch points to update (small, all mechanical)

1. **`test/regression_cases.sh`** — the `DECK_FILE` and `DECK_PROF` maps
   (@17–31) hardcode `decks/<name>.*`; repoint to `decks/<name>/<name>.*`.
2. **`.gitignore`** — line 95 `decks/*.keepmodel.exhaustive*.json` only matches
   one level; change to `decks/**/*.keepmodel.exhaustive*.json` (still ignores
   the uncompressed `.raw.json`; `.raw.json.gz` stays trackable, unaffected).
3. **Docs referencing flat paths** — `CLAUDE.md` (the "Deck files live under
   `decks/`" convention block explicitly says `decks/<name>.txt` +
   `decks/<name>.profile.json`), plus the `analyze-deck` / `regression-testing`
   / `mulligan-profile` skills and any `test/*.sh` generators that hardcode
   `decks/<foo>...`. Grep `decks/` across `*.md` and `test/*.sh` and update.
4. **Engine (`MulliganProfileIO.h`) and `analyze_deck.py`** — **no change**
   (directory-relative, per above). Verify with a smoke run after the move.

## Migration mechanics

- Per deck: `git mv` the **tracked** canonical artifacts into `decks/<name>/`
  (preserves history), then `mv` any untracked files that belong.
- **Stray experimental variants** (`slivers_vial.keepmodel.exhaustive.r20/r80/
  r100.*`, `test_deck.*`, `*.NEW.*`, `*.keepmodel.{regret,score,hybrid}.profile.json`)
  are mostly untracked scratch — decide per-deck whether to fold into the folder,
  drop into a `decks/<name>/scratch/` (gitignored), or delete. Deletion is the
  user's call; do not delete untracked artifacts unprompted.
- **`references/` is a separate top-level dir, NOT under `decks/`** — untouched
  by this change, and its COMMIT-ONLY rule is unaffected.
- After the move: run `bash test/regression.sh --smoke` to confirm every
  deck's decklist + profile + siblings still resolve (fingerprints must be
  **byte-identical** — this is a path move, nothing computational changed).

## Open decision (settle before executing)

Confirm Option A (`decks/<name>/<name>.*`) vs any preference for a different
in-folder naming. Recommendation: Option A, for the zero-code-change property.
