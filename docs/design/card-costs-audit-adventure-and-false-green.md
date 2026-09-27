# `card_costs` audit: Adventure cards misread + a rate-limited run passes falsely (FIXED)

**Status: BOTH BUGS FIXED 2026-09-27.** This note was filed as deferred evidence; the analysis below
was correct on both counts and both suggested fixes have been applied, with a network-free regression
guard. Kept as-is below the line because the *reasoning* is the valuable part — the fix record is
here at the top.

| | fix | guard |
|---|---|---|
| Bug 1 (false RED) | `scripts/audit_card_costs.py` — a face-name match now wins **even when the top-level cost is filled**, which is the adventure case. Fallbacks for MDFC and for a split card entered as `"A // B"` are preserved. | `test/audit_card_costs_selftest.py` checks 1, 1b, 3, 4 |
| Bug 2 (false GREEN) | Unresolved cards are split by **reason**: a Scryfall 404 is a genuinely custom card and stays non-fatal, while 429/timeout/DNS/5xx is a **did-not-run** — new **exit code 2**, and the audit no longer prints "All mana costs match" over zero comparisons. `verify_deck.py`'s `gate_card_costs` reports rc 2 as `cost audit INCOMPLETE -- N cost(s) UNVERIFIED`, never as a mismatch (the same doctrine its rc-124 branch already used). A mismatch still outranks incompleteness for the rc, but the partial coverage is disclosed in the same summary. | checks 2, 2b, 2c |

`cards.json` was never wrong: Brightcap Badger is `{3}{G}` and Fungus Frolic is `{2}{G}`, both
confirmed against live Scryfall (`layout: adventure`, faces carrying the correct per-half costs).
The Fungus `card_costs` gate now passes on its own merits rather than because nothing was compared.

**Live confirmation, and it exercises both fixes in one run.** A full sweep over `cards.json` after
the fix:

```
Checked 326 costed cards against Scryfall; 238 actually compared.
All 238 compared mana costs match Scryfall.
NOT COMPARED (88) -- Scryfall unreachable/rate-limited ...        rc=2
```

Brightcap Badger and Fungus Frolic are in the 238 that **compared and matched** — Bug 1 gone on the
live path. The same run shows Bug 2's fix earning its keep: 88 cards were 429'd, and the old code
would have printed "All mana costs match Scryfall" and exited **0** over those 88. One Fungus card
(Mycoloth) was among them, and a targeted re-run cleared it at `rc=0`, so every costed Fungus card
is now verified against live Scryfall. Basic lands carry no printed cost and are skipped by design.

**Expect rc 2 to be common, and do not "fix" it by reverting.** The audit covers all of `cards.json`,
not just the deck under test, so any 429 anywhere makes the gate report INCOMPLETE. That is the
point: it is the difference between "verified" and "never asked". Space the networked audits out and
re-run.

**One correction to the note below, and it strengthens its case.** The offline repro cannot be
cleared by the Bug 1 fix, because the committed snapshot `scryfall_reference.json` stores only the
joined string and **no `card_faces`** for either name — there is no face to select. That is exactly
why the snapshot-based `card_fields` gate still needs its reviewed allowlist entry, which is
untouched here. The face fix applies to the **live** path, which does return faces. Deliberately
not changed: the snapshot builder and `scryfall_divergences.json`. Making the snapshot store
per-face costs would render those user-reviewed allowlist entries STALE — which `verify_deck`
reports as blocking — so it would trade a real fix for a new false red.

**Bug 2 was then demonstrated live, unprompted.** Two `verify_deck` runs of the same deck on the
same commit, launched concurrently by one agent, disagreed purely on rate-limiting:

```
run A:  [FAIL ] card_costs   2 Scryfall cost mismatch(es)      <- resolved; found Bug 1
run B:  [PASS ] card_costs   all mana costs match Scryfall     <- 429'd; compared nothing
```

The concurrency that triggers the 429 is partly self-inflicted — running two networked audits at
once is enough to do it. Reading run B is how this was first reported as "not reproducible".

---

**Original note (as filed):** deferred 2026-09-27, evidence only, no fix applied. Found while
analyzing Pirates, whose `verify_deck` run hit the Fungus `card_costs` failure; another agent
reported it as not reproducible. This note shows it is, and why it can look otherwise.

Checked at `phase-1-2-deck-analyzer` @ `42210a4f` (2026-09-27). The two repro scripts are in the
appendix and need NO network. Save them and run them from the repo root:

```
python3 repro_offline.py       # -> 2 MISMATCHES, exit code 1
python3 repro_ratelimited.py   # -> "All mana costs match", exit code 0
```

Each script imports `scripts/audit_card_costs.py` and calls its own `main()`. Only `fetch()` is
replaced: the first script uses the committed Scryfall snapshot, the second uses the response a
rate-limited run gets. Nothing else in the audit is reimplemented.

## Why it looked "not reproducible": two bugs

### Bug 1 (the reported failure): the audit misreads Adventure cards

What Scryfall returns:
- For the combined card Brightcap Badger // Fungus Frolic, Scryfall's top-level `mana_cost` is
  `"{3}{G} // {2}{G}"`.
- The committed snapshot `src/cards/data/scryfall_reference.json` stores that string for both names.
- Its builder takes the top-level value first (`scripts/audit_card_fields.py:163`), so the snapshot
  holds exactly what the API returns.

What the audit does with it:
- `scripts/audit_card_costs.py:78-87` only picks the card half whose name matches when the top-level
  `mana_cost` is EMPTY (`if not sf_cost and "card_faces" in sf`).
- For an Adventure card the top-level field is not empty, so each half is compared against the joined
  string:
  - `Brightcap Badger   local={3}{G}   scryfall={3}{G} // {2}{G}` -> MISMATCH
  - `Fungus Frolic      local={2}{G}   scryfall={3}{G} // {2}{G}` -> MISMATCH
- It then exits 1 (`:111`), and `verify_deck.py` `gate_card_costs` reports a blocking FAIL with 2
  findings.

**The card data is correct.** Brightcap Badger is {3}{G} (the 3/4 creature) and Fungus Frolic is
{2}{G} (the Adventure instant).

Why `card_fields` passes on the same data:
- The snapshot check also compares against the joined string, but it has an allowlist.
- `src/cards/data/scryfall_divergences.json:33-38` explicitly lists this mismatch ("Scryfall reports
  the COMBINED card, so its mana_cost is the pair '{3}{G} // {2}{G}'").
- `card_costs` has no allowlist.

### Bug 2 (why a live run can pass): a rate-limited run reports a false green

Scryfall has been returning HTTP 429 `rate_limited` repeatedly today, probably because several
agents are querying it at once. When `fetch()` runs out of retries:
- The card goes into "NOT RESOLVED", which does not change the exit code (`verify_deck.py` says so
  itself: "an unresolved card does not move its rc").
- With every card unresolved, the audit prints **"All mana costs match Scryfall."** and exits 0.
  `repro_ratelimited.py` shows this.
- `verify_deck` therefore reports `card_costs` PASS while having compared nothing.

A live run made under rate limiting CANNOT reproduce Bug 1. Check its output for a
`NOT RESOLVED ... HTTP 429` block.

Other ways the failure disappears:
- `verify_deck --no-network` skips `card_costs` entirely (SKIP).
- Running `audit_card_fields.py`, the snapshot check, passes because of the allowlist above.

## Suggested fixes — BOTH APPLIED (see the fix record at the top)

1. **Adventure cards:** whenever `card_faces` is present, compare against the half whose `name`
   equals the cards.json entry, whether or not the top-level cost is filled. Keep face 0 as the
   fallback for a split card named "A // B". This clears Fungus without any sign-off and covers
   future Adventure cards.
   → **Applied as described.** One refinement: face 0 is the fallback only when the top-level cost
   is *empty*; when it is filled and no face name matches (a split card entered as `"A // B"`) the
   joined top-level string is kept, since that is what such an entry should diff against.
2. **False green:** a card that could not be resolved because of HTTP 429 or a network error must not
   let the audit report "All mana costs match". Either exit non-zero (did-not-run, like the gate's
   existing rc 124 branch), or have `gate_card_costs` treat any `HTTP 429`/network NOT RESOLVED line
   as a did-not-run FAIL. Genuinely custom cards (Scryfall 404) can stay non-fatal.
   → **Applied, taking both halves of the suggestion:** the audit exits 2 *and* the gate reports
   that rc as an explicit did-not-run. One implementation detail worth knowing if you touch
   `fetch()`: its `"HTTP 429 (exhausted retries)"` string at the end of the retry loop is
   **unreachable** — the final attempt returns from the `HTTPError` branch as plain `"HTTP 429"` —
   so classification must match on the substring `429`, never on the longer message.

## Still open (not part of this fix)

- `scryfall_reference.json` holds no `card_faces`, so the **offline** `card_fields` check still
  depends on a reviewed allowlist entry for these two names. Teaching the snapshot builder to store
  per-face costs would let that entry retire, but it invalidates the existing reviewed entries and
  so needs the user's sign-off — it is a snapshot-format change, not a bug fix.
- Nothing rate-limits the audits against *each other*. Two concurrent networked gates 429 each
  other reliably; with Bug 2 fixed that is now loud (rc 2) rather than silent, which is the
  important half, but a shared throttle would avoid the wasted run.

## Appendix: repro scripts

`repro_offline.py`:

```python
#!/usr/bin/env python3
"""Offline repro: run audit_card_costs.py's OWN per-card logic with fetch() replaced by the committed
Scryfall snapshot (src/cards/data/scryfall_reference.json, whose builder stores Scryfall's TOP-LEVEL
mana_cost first -- audit_card_fields.py:163). No network, so no rate limit. Run from the repo root."""
import json, sys
sys.path.insert(0, "scripts")
import audit_card_costs as a
snap = json.load(open("src/cards/data/scryfall_reference.json"))
cards = json.load(open("src/cards/data/cards.json")); cards = cards["cards"] if isinstance(cards, dict) else cards
a.fetch = lambda name: snap.get(name, {"_error": "not in snapshot"})
a.time.sleep = lambda s: None
sys.argv = ["audit_card_costs.py"]
wanted = {"Brightcap Badger", "Fungus Frolic"}
a.json.load = (lambda orig: (lambda fh: [c for c in (lambda d: d["cards"] if isinstance(d, dict) else d)(orig(fh)) if c["name"] in wanted]))(json.load)
rc = a.main()
print("exit code:", rc)
```

`repro_ratelimited.py` is the same script with one line changed: `fetch()` returns what a
rate-limited live run gets.

```python
a.fetch = lambda name: {"_error": "HTTP 429 (exhausted retries)"}   # what a rate-limited live run gets
```
