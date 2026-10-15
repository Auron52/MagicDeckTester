#!/usr/bin/env python3
"""Build ONE pooled manifest comparing KittyEquipment v2 vs v3 at searched depths, at power.

THE QUESTION (USER 2026-10-14): *"before any regression test rows are dropped I would like to see
that KittyV3 beats KittyV2"* / *"I believe it will, but we need actual numbers."*

*** IT RAN, AND v3 WON. v3 WAS PROMOTED TO THE SHIPPED LIST ON THE STRENGTH OF IT. ***
  d5 b40   48,000 games/arm   v2 3.8820 -> v3 3.8265   -0.0555   t -7.27 / -14.85 clustered
  d7 b1000  2,000 games/arm   v2 3.8145 -> v3 3.7465   -0.0680   direction check only
Also 1.82x cheaper per game at d5; unwon 1.44% -> 1.25%. Read the whole story -- including why the
per-cell spread came in at HALF the sampling prediction and why that was BENIGN here but fatal in
the first attempt -- in docs/design/kitty-v3-promotion-2026-10-14.md.

The `v3` arm below is now `decks/KittyEquipment` (the promoted top-level list), not the old
v3-adelbert-gauntlets folder. Re-running this reproduces the comparison against whatever ships.

WHY A NEW RUN. The suite's own rows gave d0 -0.0781 at t=-3.88 (8,000 games/arm) but searched only
-0.0205 at t=-0.46 on 1,500 games/arm. Sign favourable everywhere, nothing established where it
matters. se scales as 1/sqrt(n), so resolving a 0.02-turn effect at t=2 needs se ~0.0101, i.e.
~19x the searched sample: 28,800 searched games per arm.

EACH LIST RUNS ITS OWN SHIPPED ARTIFACTS. This is a comparison of two shipping configurations, not
a card screen -- so deck_compare.py is the WRONG tool here (Rule 0: all its arms share ONE
apparatus, and the whole point is that v2 and v3 have different keep tables and value sidecars).
Each job names its own deck's profile, and the keep table attaches by sidecar PRESENCE.

CONDITIONS MIRROR THE SUITE EXACTLY, and asymmetry here would confound the answer:
  * d3 pins `depth` and carries `ignore_play_profile: true` (the shallow-coverage shape);
  * d5 OMITS `depth` so the deck's own value_play block owns it, keeping budget_ms as the knob.
Verified both lists' value_play blocks are identical in the fields that could differ (neither sets
`enabled` or a play `depth`; both mull_gen_depth=2 / mull_gen_budget_ms=3), so d5 is symmetric.

NOT PAIRED, and it cannot be: a different decklist changes the card numbering, so the same seed
opens a different game. These are two independent samples of the same metric at the same settings,
so se is the full between-game variance -- which is exactly why the sample has to be this large.

SEEDS ARE FRESH (20001..20016), disjoint from every GT seed (1001/2002/3003/4004/5005/6006/7007/
8008/10010), so nothing here is measured on games either list's artifacts were fitted or accepted on.

PRE-REGISTERED READING, so the verdict is not chosen after the fact:
  * v3 faster at t <= -2 on searched games  -> "V3 beats V2", the user's gate is met.
  * |t| < 2                                 -> NOT a win. Report it as "indistinguishable at
    28,800 games/arm", state the bound the sample can exclude, and let the user decide whether
    not-worse-plus-cheaper satisfies the gate. A null must not be reported as a win.
  * v3 slower at t >= +2                    -> V2 is better on searched play; rows stay, and the
    d0 result needs explaining.
"""
import json
import os

ROOT = "/workspaces/MagicDeckTester"
ARMS = {
    "v2": "decks/KittyEquipment/v2-puresteel-hammer",
    "v3": "decks/KittyEquipment",
}
# *** SEED BASES MUST BE SPACED BY MORE THAN THE GAME COUNT. ***
# A batch job's per-game seed is `seed_base + game_index` (test/.../batch repro = --seed base+gi).
# So base=20001 with 1200 games covers game seeds 20001..21200, and base=20002 covers 20002..21201
# -- an overlap of 1199 of 1200. The FIRST version of this script used bases 20001..20016 with 1200
# games and therefore replayed ONE game set 16 times: verified afterwards, s20001[gi+1] ==
# s20002[gi] for 1199 of 1199 shifted pairs, and the 16 cell deltas spanned just -0.080..-0.074
# (sd 0.0017, where independent sampling would give ~0.057). The distinct sample was ~1,215 games
# per arm, not 19,200, so the reported t was inflated by ~sqrt(16) = 4x. The suite gets this right
# by spacing its bases ~1001 apart and keeping counts below that; this script now ASSERTS it.
# USER 2026-10-14: *"the higher depth is the number that matters"* and *"I'm not so worried about
# d3"* -- so d3 is DROPPED and the sample goes where the decision is.
#   d5 b40    the suite's shipped searched condition, and the primary. Sized for POWER: the first
#             (overlap-ruined) read put d5 at -0.0167, and resolving that at t=2 needs se ~0.008,
#             i.e. ~48,000 games per arm at a per-game sd of ~1.3.
#   d7 b1000  *"a few games at high depth and budget"* -- a DIRECTION CHECK at the top of the
#             range, deliberately small. At 2,000 games/arm its se is ~0.041, so it can only see an
#             effect of ~0.08+; it is NOT a powered test and must not be quoted as one. Sized small
#             on purpose because its per-game cost is unmeasured for these decks -- read the cost
#             off this run and power it properly afterwards if the direction warrants it.
CELLS = [(5, 40, 1500, 100000, 32), (7, 1000, 250, 200000, 8)]
# (depth, budget_ms, games_per_base, seed_origin, n_bases)
SEED_STRIDE = 2000                          # > every game count above

jobs = []
seen = []
for depth, bud, games, origin, nb in CELLS:
    assert SEED_STRIDE > games, f"d{depth}: stride {SEED_STRIDE} must exceed {games} games"
    for i in range(nb):
        b = origin + i * SEED_STRIDE
        seen.append((b, b + games - 1))
seen.sort()
for (lo1, hi1), (lo2, hi2) in zip(seen, seen[1:]):
    assert hi1 < lo2, f"game-seed ranges overlap: {lo1}..{hi1} and {lo2}..{hi2}"
assert seen[0][0] > 10010, "must not reuse any committed GT seed base"

for arm, d in ARMS.items():
    deck = f"{d}/KittyEquipment.cod"
    prof = f"{d}/KittyEquipment.profile.json"
    for f in (deck, prof):
        assert os.path.exists(os.path.join(ROOT, f)), f
    # each arm must actually HAVE its keep table, or this measures a missing-sidecar arm
    gz = f"{d}/KittyEquipment.keepmodel.exhaustive.profile.json.gz"
    assert os.path.exists(os.path.join(ROOT, gz)), f"{arm} has no keep table: {gz}"
    for depth, bud, games, origin, nb in CELLS:
        for i in range(nb):
            s = origin + i * SEED_STRIDE
            j = {"name": f"{arm}_d{depth}_s{s}", "deck": deck, "profile": prof,
                 "games": games, "seed": s, "budget_ms": bud,
                 # expensive jobs first so the cheap ones backfill the tail
                 "weight": depth * 1000 + bud}
            if depth != 5:                  # mirror regression.sh: d5 lets value_play own the depth
                j["depth"] = depth
                j["ignore_play_profile"] = True
            jobs.append(j)

out = os.path.join(ROOT, "logs/kittyv3_integrate/v2_vs_v3.manifest.json")
with open(out, "w") as fh:
    json.dump({"jobs": jobs}, fh, indent=1)
    fh.write("\n")

per_arm = sum(g * nb for _, _, g, _, nb in CELLS)
print(f"wrote {out}")
print(f"  {len(jobs)} jobs, {sum(j['games'] for j in jobs):,} games "
      f"({per_arm:,} DISTINCT searched games per arm)")
for depth, bud, games, origin, nb in CELLS:
    last = origin + (nb - 1) * SEED_STRIDE
    print(f"  d{depth} b{bud}: {nb} bases {origin}..{last} step {SEED_STRIDE} x {games} "
          f"= {nb*games:,} games/arm  (game seeds {origin}..{last+games-1}, no overlap)")
