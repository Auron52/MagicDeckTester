#!/usr/bin/env python3
"""Is a BOARD ACTIVATION's precondition ever REACHED? (worked case: Stoneforge Mystic's put)

The 5h gate reports `tap_put_from_hand_cost -> NOT_FORCED` for KittyEquipment: Stoneforge
reaches play but no enumerated plan action ever carries `verb:sfput`. Two very different
causes fit that observation:

  (a) the state is never reached  -- Stoneforge dies / the Equipment is cast first / there
      is never {1}{W} spare on a turn Stoneforge is untapped and not summoning-sick, or
  (b) an upstream gate drops the action even though the state IS live (a wiring gap).

This separates them by reconstructing the precondition from the decision JSON alone:
Stoneforge on my battlefield at a pre_main decision, present at an EARLIER turn's decision
too (so it is not summoning-sick), an Equipment in hand, and enough lands untapped to pay
{1}{W} on top of the turn's other business. Then it asks whether the emitted plan list
contains an action with verb `sfput`.
"""
import subprocess, sys, json, re, os, collections

BIN  = os.environ.get("MTG_BIN", "./build/Release/mtg")
DECK = "decks/KittyEquipment/KittyEquipment.cod"
PROF = "decks/KittyEquipment/KittyEquipment.profile.json"
DEC_RE = re.compile(r"<<<CLAUDE_DECISION>>>\n(.*?)\n<<<END_DECISION>>>", re.S)
RES_RE = re.compile(r"<<<CLAUDE_RESULT>>>")

EQUIP = {"Colossus Hammer", "Bonesplitter", "Loxodon Warhammer", "Shadowspear",
         "Grafted Wargear", "O-Naginata", "Lightning Greaves", "Umezawa's Jitte"}


def step(seed, gi, choices, jitte, max_turns):
    cmd = [BIN, DECK, "--profile", PROF, "--claude-play", "--seed", str(seed + gi),
           "--game-index", str(gi), "--max-turns", str(max_turns),
           "--reveal", "6", "--choices", ",".join(map(str, choices)), "--jitte-prompt"]
    if jitte:
        cmd += ["--jitte", ",".join(jitte)]
    out = subprocess.run(cmd, capture_output=True, text=True,
                         env=dict(os.environ, MTG_PLAY_PLANS_CAP="0")).stdout
    if RES_RE.search(out):
        return None
    m = DEC_RE.search(out)
    return json.loads(m.group(1)) if m else None


def pick(d):
    t = d.get("type")
    if t == "mulligan":
        ac = d.get("ai_choice");  return ac if isinstance(ac, int) else 1
    if t == "bottom":
        ac = d.get("ai_choice")
        if isinstance(ac, dict): return ac.get("index", 0)
        return ac if isinstance(ac, int) else 0
    if t == "main_phase":
        plans = d.get("plans", [])
        if not plans: return -1
        return max(plans, key=lambda p: len(p.get("summary", "")))["index"]
    if "heuristic_default" in d:
        return d["heuristic_default"]
    return 0


def run_game(seed, gi, max_turns, hits):
    choices, jitte = [], []
    seen_sf_turn = None          # earliest turn Stoneforge was seen on my battlefield
    for _ in range(400):
        d = step(seed, gi, choices, jitte, max_turns)
        if d is None:
            break
        if d.get("type") == "jitte":
            jitte.append("%d:%d" % (d.get("turn", 0), d.get("heuristic_default", 0)))
        if d.get("type") == "main_phase":
            me = d.get("me", {})
            bf = me.get("battlefield", [])
            names = [p.get("name") for p in bf]
            turn = d.get("turn", 0)
            if "Stoneforge Mystic" in names and seen_sf_turn is None:
                seen_sf_turn = turn
            live = ("Stoneforge Mystic" in names
                    and seen_sf_turn is not None and turn > seen_sf_turn)
            hand_eq = [c["name"] for c in me.get("hand", []) if c.get("name") in EQUIP]
            lands = sum(1 for p in bf if p.get("is_land"))
            has_sol = "Sol Ring" in names
            offered = any(a.get("verb") == "sfput"
                          for p in d.get("plans", []) for a in p.get("actions", []))
            if live and hand_eq:
                hits["state_live"] += 1
                key = (seed, gi, turn)
                hits["detail"].append({
                    "seed": seed, "gi": gi, "turn": turn, "phase": d.get("phase"),
                    "equip_in_hand": hand_eq, "lands": lands, "sol_ring": has_sol,
                    "sfput_offered": offered, "n_plans": len(d.get("plans", [])),
                })
                if offered:
                    hits["offered"] += 1
            if "Stoneforge Mystic" in names:
                hits["sf_in_play_decisions"] += 1
        c = pick(d)
        if isinstance(c, list): choices.extend(c)
        else:                   choices.append(c)
    if seen_sf_turn is not None:
        hits["games_with_sf"] += 1


def main():
    base = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    n    = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    mt   = int(sys.argv[3]) if len(sys.argv) > 3 else 14
    hits = collections.Counter()
    hits["detail"] = []
    for gi in range(n):
        run_game(base, gi, mt, hits)
    print(json.dumps({
        "games": n, "games_with_stoneforge": hits["games_with_sf"],
        "decisions_with_sf_in_play": hits["sf_in_play_decisions"],
        "decisions_with_live_put_state": hits["state_live"],
        "of_those_sfput_offered": hits["offered"],
    }, indent=1))
    for r in hits["detail"]:
        print(" ", json.dumps(r))


main()
