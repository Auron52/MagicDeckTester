#!/usr/bin/env python3
"""Self-test for scripts/audit_card_costs.py. NO NETWORK -- run it from the repo root:

    python3 test/audit_card_costs_selftest.py     # -> 9/9 checks passed, exit 0

It replaces ONLY fetch() (with LIVE-SHAPED payloads recorded from api.scryfall.com) and the cards
list; every comparison is the audit's own code, so this pins behaviour rather than restating it.

It exists because the cost audit failed in both directions at once on 2026-09-27, and neither
failure was visible from its exit code (see docs/design/card-costs-audit-adventure-and-false-green.md):

  * FALSE RED -- an ADVENTURE card (Brightcap Badger // Fungus Frolic) was diffed against
    Scryfall's JOINED top-level mana_cost "{3}{G} // {2}{G}", so both halves "mismatched" on card
    data that is correct. Face selection was gated on the top-level cost being EMPTY, which holds
    for an MDFC but not for an adventure.
  * FALSE GREEN -- when Scryfall 429s, an unresolved card did not move the exit code, so a fully
    rate-limited run printed "All mana costs match Scryfall" and exited 0 having compared NOTHING.
    Two concurrent verify_deck runs of the SAME deck on the SAME commit disagreed (one FAIL, one
    PASS) purely on whether they had been rate-limited.

The false green is the more dangerous of the two, so the rate-limit checks below are the ones to
keep working: a gate that cannot tell "verified" from "never asked" is worse than no gate.
"""
import io, json, sys, contextlib
sys.path.insert(0, "scripts")
import audit_card_costs as a
a.time.sleep = lambda s: None
sys.argv = ["audit_card_costs.py"]

# --- live-shaped Scryfall payloads (adventure verified against the API this session) ---
ADVENTURE = {"layout": "adventure", "mana_cost": "{3}{G} // {2}{G}", "cmc": 4.0, "card_faces": [
    {"name": "Brightcap Badger", "mana_cost": "{3}{G}", "cmc": None},
    {"name": "Fungus Frolic",    "mana_cost": "{2}{G}", "cmc": None}]}
MDFC = {"layout": "modal_dfc", "mana_cost": "", "cmc": 5.0, "card_faces": [
    {"name": "Jorn, God of Winter",      "mana_cost": "{2}{G}"},
    {"name": "Kaldring, the Rimestaff",  "mana_cost": "{1}{B}"}]}
SPLIT = {"layout": "split", "mana_cost": "{1}{R} // {2}{R}", "cmc": 2.0, "card_faces": [
    {"name": "Bond", "mana_cost": "{1}{R}"}, {"name": "Break", "mana_cost": "{2}{R}"}]}
PLAIN = {"layout": "normal", "mana_cost": "{5}{G}{G}", "cmc": 7.0}

def run(cards, fetch):
    a.fetch = fetch
    orig = json.load
    a.json.load = lambda fh: cards
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = a.main()
    a.json.load = orig
    return rc, buf.getvalue()

def check(label, ok, rc, out, want_rc):
    status = "PASS" if (ok and rc == want_rc) else "FAIL"
    print(f"[{status}] {label}  (rc={rc}, want {want_rc})")
    if status == "FAIL":
        print("  ---- output ----"); print("  " + out.replace("\n", "\n  "))
    return status == "PASS"

results = []

# 1. THE REPORTED FAILURE: both adventure halves must now MATCH.
cards = [{"name": "Brightcap Badger", "mana_cost": "{3}{G}"},
         {"name": "Fungus Frolic",    "mana_cost": "{2}{G}"}]
rc, out = run(cards, lambda n: ADVENTURE)
results.append(check("adventure halves compare per-face (was 2 mismatches)",
                     "MISMATCH" not in out and "All 2 compared" in out, rc, out, 0))

# 1b. and a REAL adventure miscost must still be caught (the fix must not blind the gate).
rc, out = run([{"name": "Fungus Frolic", "mana_cost": "{1}{G}"}], lambda n: ADVENTURE)
results.append(check("a genuine adventure-half miscost is still caught",
                     "MISMATCHES (1)" in out and "scryfall={2}{G}" in out, rc, out, 1))

# 2. THE FALSE GREEN: an all-429 run must NOT claim a clean bill of health.
rc, out = run(cards, lambda n: {"_error": "HTTP 429"})
results.append(check("rate-limited run is a did-not-run, not a pass",
                     "All" not in out and "NOTHING WAS COMPARED" in out
                     and "NOT COMPARED (2)" in out, rc, out, 2))

# 2b. a 404 (genuinely custom card) stays non-fatal ALONGSIDE a card that did compare -- this is
#     the policy check: an unknown custom card must not fail the audit for the rest of the file.
rc, out = run([{"name": "Totally Custom Card", "mana_cost": "{1}"},
               {"name": "Annoyed Altisaur", "mana_cost": "{5}{G}{G}"}],
              lambda n: {"_error": "HTTP 404"} if n == "Totally Custom Card" else PLAIN)
results.append(check("a Scryfall 404 custom card stays non-fatal",
                     "NOT RESOLVED (1)" in out and "NOT COMPARED" not in out
                     and "All 1 compared" in out, rc, out, 0))

# 2b'. but a run that compared NOTHING is a did-not-run whatever the reason -- including all-404.
rc, out = run([{"name": "Totally Custom Card", "mana_cost": "{1}"}], lambda n: {"_error": "HTTP 404"})
results.append(check("zero comparisons is a did-not-run even with no rate limit",
                     "NOTHING WAS COMPARED" in out and "All" not in out, rc, out, 2))

# 2c. MIXED: a real mismatch plus an unreachable card reports BOTH.
mixed = [{"name": "Fungus Frolic", "mana_cost": "{1}{G}"}, {"name": "Annoyed Altisaur", "mana_cost": "{5}{G}{G}"}]
rc, out = run(mixed, lambda n: ADVENTURE if n == "Fungus Frolic" else {"_error": "HTTP 429"})
results.append(check("mismatch + unreachable discloses both (rc prefers the mismatch)",
                     "MISMATCHES (1)" in out and "NOT COMPARED (1)" in out, rc, out, 1))

# 3. REGRESSION: the MDFC back-face case (Kaldring) must still select by name.
rc, out = run([{"name": "Kaldring, the Rimestaff", "mana_cost": "{1}{B}"}], lambda n: MDFC)
results.append(check("MDFC back face still selected by name (Kaldring)",
                     "MISMATCH" not in out, rc, out, 0))

# 4. REGRESSION: a split card entered by its full "A // B" name keeps the joined top-level cost.
rc, out = run([{"name": "Bond // Break", "mana_cost": "{1}{R} // {2}{R}"}], lambda n: SPLIT)
results.append(check("split card named 'A // B' keeps the joined cost",
                     "MISMATCH" not in out, rc, out, 0))

# 5. REGRESSION: cascade_max_mv cross-check still fires on a single-faced card.
rc, out = run([{"name": "Annoyed Altisaur", "mana_cost": "{5}{G}{G}",
                "parameters": {"cascade_max_mv": 6}}], lambda n: PLAIN)
results.append(check("cascade_max_mv mismatch still caught (6 vs cmc 7)",
                     "cascade_max_mv=6" in out and "cmc=7" in out, rc, out, 1))
rc, out = run([{"name": "Annoyed Altisaur", "mana_cost": "{5}{G}{G}",
                "parameters": {"cascade_max_mv": 7}}], lambda n: PLAIN)
results.append(check("cascade_max_mv correct value passes", "MISMATCH" not in out, rc, out, 0))

# 6. A double-faced card carrying cascade_max_mv: Scryfall reports cmc only for the WHOLE card, so
#    the per-face mv cannot be diffed. Disclose it, do NOT diff against the wrong number, and do
#    NOT fail -- a permanently-red gate nobody can clear is the false-red mirror of the false green.
rc, out = run([{"name": "Fungus Frolic", "mana_cost": "{2}{G}",
                "parameters": {"cascade_max_mv": 3}}], lambda n: ADVENTURE)
results.append(check("double-faced cascade card: disclosed, not diffed, not fatal",
                     "CASCADE NOT CROSS-CHECKED (1)" in out and "MISMATCH" not in out
                     and "NOT COMPARED" not in out, rc, out, 0))

print(f"\n{sum(results)}/{len(results)} checks passed")
sys.exit(0 if all(results) else 1)
