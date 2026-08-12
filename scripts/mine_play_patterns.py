#!/usr/bin/env python3
"""Mine consistent play patterns from teacher games (prototype).

The heuristics-generating loop's first stage (user direction, 2026-08-12): instead of a human
authoring every prune, aggregate what the TEACHER consistently does across recorded games and
surface near-invariant patterns -- each with its support count and every exception -- so a
human only APPROVES rules. Approved patterns become provider prunes behind an MTG_UNPRUNE
gate and go through the standard train/held-out validation (heuristic-optimization skill).

Sources this prototype reads:
  references/<deck>/claude_s*_gi*.json   hand-played games in the viewer protocol: every
                                         main-phase decision carries the FULL enumerated plan
                                         list plus the picked index -- (state, chosen,
                                         rejected) triples, the strongest mining signal.

Usage:
  python3 scripts/mine_play_patterns.py references/Mirrorwing_Dragon

Pattern templates are deck-agnostic; card roles (magnet / trick / dork / land) are resolved
from src/cards/data/cards.json parameters, never hard-coded names.
"""
import json, os, re, sys, glob
from collections import defaultdict

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def load_roles():
    db = json.load(open(os.path.join(ROOT, "src/cards/data/cards.json")))
    cards = db["cards"] if isinstance(db, dict) and "cards" in db else db
    roles = {"magnet": set(), "trick": set(), "dork": set(), "creature": set(), "land": set()}
    for c in cards:
        n = c.get("name", "")
        p = c.get("parameters", {}) or {}
        t = [x.lower() for x in c.get("types", [])]
        if p.get("copies_solo_targeted_spells"):
            roles["magnet"].add(n)
        if p.get("solo_target_trick"):
            roles["trick"].add(n)
            if p.get("creates_treasures", 0) > 0:
                roles.setdefault("treasure_trick", set()).add(n)
        if c.get("template") == "mana_dork":
            roles["dork"].add(n)
        if "creature" in t:
            roles["creature"].add(n)
        if "land" in t:
            roles["land"].add(n)
    return roles


def plan_trick_targets(plan, roles):
    """(trick_name, target_name|None) pairs for solo tricks cast by this plan."""
    out = []
    for a in plan.get("actions", []):
        if a.get("card") in roles["trick"]:
            out.append((a["card"], a.get("enchant_target_name")))
    return out


class Pattern:
    def __init__(self, key, describe):
        self.key, self.describe = key, describe
        self.follow, self.violate = 0, 0
        self.exceptions = []          # (ref, decision_index, detail), capped

    def note(self, followed, ref, di, detail=""):
        if followed:
            self.follow += 1
        else:
            self.violate += 1
            if len(self.exceptions) < 10:
                self.exceptions.append((ref, di, detail))

    @property
    def n(self):
        return self.follow + self.violate

    def report(self):
        if self.n == 0:
            return f"  [no data]      {self.describe}"
        pct = 100.0 * self.follow / self.n
        line = f"  {pct:5.1f}% (n={self.n:3d})  {self.describe}"
        for ref, di, detail in self.exceptions:
            line += f"\n      exception: {os.path.basename(ref)} decision {di}  {detail}"
        return line


def mine_references(ref_dir, roles):
    pats = {
        "magnet_target": Pattern(
            "magnet_target",
            "magnet on battlefield & chosen plan casts a solo trick -> trick targets the magnet"),
        "magnet_target_offered": Pattern(
            "magnet_target_offered",
            "magnet on battlefield & a magnet-targeted plan existed -> a trick cast this turn used it"),
        "t1_dork": Pattern(
            "t1_dork",
            "turn 1, a dork-casting plan exists -> chosen plan casts a dork"),
        "gr_bank": Pattern(
            "gr_bank",
            "no magnet on bf & Gold-Rush-class trick castable -> teacher declined to cast it"),
        "untargeted_trick": Pattern(
            "untargeted_trick",
            "magnet on battlefield -> teacher never picks an UNTARGETED up-to-one trick cast"),
    }
    n_dec = 0
    for ref in sorted(glob.glob(os.path.join(ref_dir, "claude_*.json"))):
        d = json.load(open(ref))
        for step in d.get("decisions", []):
            dec = step.get("decision", {})
            if dec.get("type") != "main_phase":
                continue
            plans = dec.get("plans", [])
            if len(plans) < 2:
                continue
            n_dec += 1
            chosen = next((p for p in plans if p.get("index") == step.get("chosen")), None)
            if chosen is None:
                continue
            bf = [b.get("name", "") for b in dec.get("me", {}).get("battlefield", [])]
            magnet_bf = any(n in roles["magnet"] for n in bf)
            di = dec.get("decision_index")
            turn = dec.get("turn")

            ct = plan_trick_targets(chosen, roles)
            if magnet_bf:
                for trick, tgt in ct:
                    pats["magnet_target"].note(tgt in roles["magnet"], ref, di,
                                               f"T{turn} {trick} -> {tgt}")
                    if tgt is None:
                        pats["untargeted_trick"].note(False, ref, di, f"T{turn} {trick} untargeted")
                    else:
                        pats["untargeted_trick"].note(True, ref, di)
                offered = any(t in roles["magnet"]
                              for p in plans for _, t in plan_trick_targets(p, roles))
                if offered:
                    used = any(t in roles["magnet"] for _, t in ct)
                    pats["magnet_target_offered"].note(used, ref, di,
                                                       f"T{turn} chose: {chosen.get('summary')}")

            if not magnet_bf:
                # Gold-Rush-class = a trick that creates Treasures (role: trick with an
                # untargeted "bank" variant offered). Was a magnetless cast OFFERED, and did
                # the teacher take it?
                gr_plans = [p for p in plans
                            if any(t for t, _ in plan_trick_targets(p, roles)
                                   if t in roles.get("treasure_trick", set()))]
                if gr_plans:
                    took = any(t in roles.get("treasure_trick", set())
                               for t, _ in ct)
                    pats["gr_bank"].note(not took, ref, di,
                                         f"T{turn} chose: {chosen.get('summary')}")

            if turn == 1:
                dork_plan = any(any(a.get("card") in roles["dork"] for a in p.get("actions", []))
                                for p in plans)
                if dork_plan:
                    chose_dork = any(a.get("card") in roles["dork"]
                                     for a in chosen.get("actions", []))
                    pats["t1_dork"].note(chose_dork, ref, di,
                                         f"chose: {chosen.get('summary')}")
    return pats, n_dec


def main():
    ref_dir = sys.argv[1] if len(sys.argv) > 1 else "references/Mirrorwing_Dragon"
    roles = load_roles()
    pats, n_dec = mine_references(ref_dir, roles)
    print(f"== mined {ref_dir}: {n_dec} multi-plan main-phase decisions ==")
    print(f"   roles: magnets={sorted(roles['magnet'])} tricks={sorted(roles['trick'])}")
    for p in pats.values():
        print(p.report())


if __name__ == "__main__":
    main()
