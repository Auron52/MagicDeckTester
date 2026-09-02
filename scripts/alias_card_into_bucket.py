#!/usr/bin/env python3
"""Substitute a card into an EXISTING keep-table bucket -- the no-regeneration route for screening.

A screen wants one shared mulligan apparatus, but the shipped table does not bucket a newly
introduced card, so DecideBottom/Decide return present=false for any hand holding it and that arm
silently falls back to the static keep. Regenerating a table is hours; a union table is banned
(deck-screening Rule 0a). This does neither: it copies the shipped table and adds the new card's
NAME to the bucket of the card it replaces, so the arm's bucket composition is IDENTICAL to base's
(e.g. Anger 3 + Libation 1 occupies the same slot as base's Anger 4). Coverage is then exact for
every arm, the apparatus is genuinely shared, and nothing is generated.

What it assumes, and it is a real assumption: that the MULLIGAN decision treats the substitute like
the card it replaces. That holds the keep/bottom policy fixed across arms by construction, which is
what ISOLATES the in-play difference -- the thing a screen is asking about. It cannot tell you that
the new card would want a different keep policy.

  alias_card_into_bucket.py <shipped-profile-or-table> <new card> <existing card> <out-dir>
"""
import json, gzip, os, shutil, sys

tbl_in, new_card, host_card, outdir = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]

def load(p):
    return json.load(gzip.open(p, "rt") if p.endswith(".gz") else open(p))

d = load(tbl_in)
ek = d["exhaustive_keep"] if isinstance(d, dict) and "exhaustive_keep" in d else d
buckets = ek["buckets"]
hit = [i for i, b in enumerate(buckets) if host_card in b]
if not hit:
    raise SystemExit(f"host card {host_card!r} is not in any bucket of {tbl_in}")
if any(new_card in b for b in buckets):
    raise SystemExit(f"{new_card!r} is ALREADY bucketed -- no alias needed")
b = hit[0]
buckets[b] = sorted(buckets[b] + [new_card])
print(f"bucket[{b}] {host_card!r} -> now {buckets[b]}")
print(f"K unchanged: {len(buckets)} buckets")

os.makedirs(outdir, exist_ok=True)
stem = os.path.basename(tbl_in).split(".keepmodel.exhaustive.profile.json")[0]
out = os.path.join(outdir, stem + ".keepmodel.exhaustive.profile.json.gz")
with gzip.open(out, "wt") as f:
    json.dump(d, f)
print(f"wrote {out}  ({os.path.getsize(out)/1e6:.1f} MB)")
