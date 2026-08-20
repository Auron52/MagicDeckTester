#!/usr/bin/env python3
"""Do the KittyEquipment order levers actually DO what the USER asked for?

"Changes play" is not the same as "implements the ruling" -- the Kemba park changed play in 4 of 150
games while (as first read) never closing its loop. So each ruling gets a direct behavioural check
against the played games, not an average.

Two rulings are checked:

1. CAST ORDER (MTG_KE_ORDER), USER 2026-08-19: "Puresteel Paladin, Stoneforge Mystic and equipment
   should be in front I think for the card draw/tutor effect. Swords and Unexpectedly are essentially
   unused in goldfish." + "Sol Ring first is probably fine."
   Checked as: within a single turn's casts, does a lower-ranked card ever get cast AFTER a
   higher-ranked one? Counted per adjacent inversion, control vs arm.

2. O-NAGINATA (MTG_EQUIP_MINPOWER_LAST), USER 2026-08-19: "O-Naginata should be equipped before
   Lightning Greaves, but last otherwise, so the power is okay."
   Checked as: in turns where O-Naginata is equipped alongside other equipment, does it land after
   the ordinary gear (so the host's power has already been raised) and before Lightning Greaves?

Usage: order_behaviour.py <ctl-dir> <arm-dir>
"""
import json
import pathlib
import sys
from collections import Counter

# The reviewed tiers, lower = earlier. Mirrors EquipmentProvider::CastOrderRank.
TIER = {
    "Sol Ring": 5,
    "Puresteel Paladin": 6,     # draw_on_equipment_etb + metalcraft
    "Stoneforge Mystic": 7,     # tap-put / equipment tutor
    # equipment = 8
    "Swords to Plowshares": 30,
    "Unexpectedly Absent": 30,
}
EQUIPMENT = {"Colossus Hammer", "Bonesplitter", "Loxodon Warhammer", "Shadowspear",
             "Grafted Wargear", "O-Naginata", "Lightning Greaves", "Umezawa's Jitte"}
GREAVES = "Lightning Greaves"
NAGINATA = "O-Naginata"


def rank(name):
    if name in TIER:
        return TIER[name]
    if name in EQUIPMENT:
        return 8
    return 10          # hosts / everything else


def casts_by_turn(g):
    """-> {(turn, phase): [card names cast, in order]}"""
    out = {}
    for e in g.get("turns", []):
        seq = [a.get("cardName") for a in e.get("actions", [])
               if a.get("type") == "CAST_SPELL" and a.get("cardName")]
        if seq:
            out[(e["turn"], e.get("phase"))] = seq
    return out


def equips_by_turn(g):
    """-> {(turn, phase, host): [equipment names attached to THAT host, in order]}.

    Grouped by host on purpose: the ruling is about the order gear lands on ONE creature -- the
    ordinary gear first so the host's power clears O-Naginata's power-3 gate, and Greaves last
    because its shroud blocks everything after it. Equips log as ABILITY with
    ability = "equip -> <host>"; Balan's "attach all equipment" and the Jitte charge modes are
    ABILITYs too and must not be mistaken for equips.
    """
    out = {}
    for e in g.get("turns", []):
        for a in e.get("actions", []):
            if a.get("type") != "ABILITY":
                continue
            ab = a.get("ability") or ""
            if not ab.startswith("equip -> "):
                continue
            name = a.get("cardName")
            if name not in EQUIPMENT:
                continue
            out.setdefault((e["turn"], e.get("phase"), ab[len("equip -> "):]), []).append(name)
    return out


def scan(root):
    inv = 0          # adjacent cast-order inversions
    pairs = 0        # adjacent cast pairs examined
    nag_after_ord = nag_before_ord = 0
    nag_before_greaves = nag_after_greaves = 0
    turns_with_nag = 0
    for f in sorted(pathlib.Path(root).glob("*.json")):
        g = json.loads(f.read_text())
        for seq in casts_by_turn(g).values():
            for a, b in zip(seq, seq[1:]):
                pairs += 1
                if rank(a) > rank(b):
                    inv += 1
        for seq in equips_by_turn(g).values():
            if NAGINATA not in seq:
                continue
            turns_with_nag += 1
            i = seq.index(NAGINATA)
            others = [x for x in seq if x not in (NAGINATA, GREAVES)]
            if others:
                last_other = max(j for j, x in enumerate(seq) if x in others)
                if i > last_other:
                    nag_after_ord += 1
                else:
                    nag_before_ord += 1
            if GREAVES in seq:
                if i < seq.index(GREAVES):
                    nag_before_greaves += 1
                else:
                    nag_after_greaves += 1
    return dict(inv=inv, pairs=pairs, turns_with_nag=turns_with_nag,
                nag_after_ord=nag_after_ord, nag_before_ord=nag_before_ord,
                nag_before_greaves=nag_before_greaves, nag_after_greaves=nag_after_greaves)


def main():
    ctl, arm = scan(sys.argv[1]), scan(sys.argv[2])
    print(f"{'':<34}{'control':>12}{'arm':>12}")
    print(f"{'adjacent cast pairs':<34}{ctl['pairs']:>12}{arm['pairs']:>12}")
    for c, a, label in [
        (ctl['inv'], arm['inv'], 'cast-order INVERSIONS (want fewer)'),
        (ctl['turns_with_nag'], arm['turns_with_nag'], 'turns equipping O-Naginata'),
        (ctl['nag_after_ord'], arm['nag_after_ord'], '  ...after ordinary gear (WANT)'),
        (ctl['nag_before_ord'], arm['nag_before_ord'], '  ...before it (violates ruling)'),
        (ctl['nag_before_greaves'], arm['nag_before_greaves'], '  ...before Greaves (WANT)'),
        (ctl['nag_after_greaves'], arm['nag_after_greaves'], '  ...after Greaves (violates)'),
    ]:
        print(f"{label:<34}{c:>12}{a:>12}")
    if ctl['pairs'] and arm['pairs']:
        print(f"\ninversion rate: control {100.0*ctl['inv']/ctl['pairs']:.2f}%"
              f"  ->  arm {100.0*arm['inv']/arm['pairs']:.2f}%")


if __name__ == "__main__":
    main()
