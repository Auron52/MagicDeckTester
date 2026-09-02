#!/usr/bin/env python3
"""Classify the divergent games of an aliased Luxurious Libation screen by MECHANISM.

The screen is a one-card paired swap on a shared apparatus: the arm's Libation INHERITS the library
slot number of the card it replaces (Anger's 4th copy / Oracle's 3rd), so both arms deal the
identical library order and exactly one physical card differs, in place. That is what makes a
per-game causal story available at all -- any divergence traces to that one card.

Everything below keys on card NAMES, never on the `card` numbers in the log. Those numbers are the
engine's own per-deck display numbering, which is rebuilt per decklist and is NOT the paired
MTG_DECK_NUMBERING that drives the shuffle -- so base's `card: 39` and the arm's `card: 39` are
different physical cards. Comparing them makes every game look like a mulligan divergence.

Classes, in the order a game is tested against them:

  MULLIGAN       the keep/bottom decision itself differed (the keep table buckets the two names
                 differently), so the arms did not even start from the same hand
  SEARCH_ONLY    the swapped card never reached either arm's hand -- the divergence is the LOOKAHEAD
                 seeing a different library composition, not a card that was ever held
  NEITHER_CAST   the swapped card reached hand and was cast in neither arm
  REPLACED_ONLY  base cast the cut card; the arm did not cast Libation -- a live card traded for a
                 stranded one
  BOTH_CAST      each arm cast its own card; the difference is what the card DID
  LIBATION_ONLY  only the arm's Libation was cast

usage: mw_libation_classify.py <anger|oracle> [--dump N]
"""
import json, glob, os, re, sys, collections, statistics as st

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WHICH = sys.argv[1] if len(sys.argv) > 1 else "oracle"
DUMP = int(sys.argv[sys.argv.index("--dump") + 1]) if "--dump" in sys.argv else 0

CFG = {
    "anger":  {"arm": "lib_over_anger",  "cut": "Ancestral Anger"},
    "oracle": {"arm": "lib_over_oracle", "cut": "Oracle's Restoration"},
}[WHICH]
ARM, CUT = CFG["arm"], CFG["cut"]
LIB = "Luxurious Libation"
SEED0 = 2600000
GL = os.path.join(ROOT, "logs/mw_libation/glogs", WHICH)


def index_logs(d):
    return {int(os.path.basename(f).split("_")[2]): f
            for f in sorted(glob.glob(os.path.join(d, "*.json")))}


def feats(log):
    # An unwon game prints no turn; the repo scores it max_turns+1 = 9, the convention the screen's
    # own aggregate uses. Dropping it instead would bias every mean here.
    won = log["result"].get("winner") == "player"
    f = {"win_turn": log["result"].get("turn") if won else 9, "won": won}
    ms = log.get("mulliganSequence", [])
    f["mulls"] = len(ms) - 1 if ms else 0
    # DECISION signature, with the swapped card normalised to the name it replaced. Without that
    # normalisation every hand holding the swapped slot reads as a different hand -- the two arms
    # would look like they mulliganed differently in every such game, when in fact the keep table
    # aliases the two names into ONE bucket and the decision was identical. What survives the
    # normalisation is a real difference: a different mulligan count, or a different bucket bottomed.
    def norm(x):
        return CUT if x == LIB else x
    f["mull_sig"] = [(m.get("attempt"), sorted(norm(c["cardName"]) for c in m.get("hand", [])),
                      sorted(norm(c["cardName"]) for c in m.get("bottomed", [])), m.get("kept"))
                     for m in ms]
    f["mull_kept_hand"] = sorted(norm(c["cardName"]) for c in log.get("openingHand", []))
    f["open"] = [c["cardName"] for c in log.get("openingHand", [])]
    casts, draws, lands, atks = [], [], [], []
    for t in log["turns"]:
        for a in t["actions"]:
            ty = a["type"]
            if ty == "CAST_SPELL":
                casts.append((t["turn"], a["cardName"], a.get("manaPaid", "")))
            elif ty == "DRAW":
                draws.append((t["turn"], a["cardName"]))
            elif ty == "PLAY_LAND":
                lands.append((t["turn"], a["cardName"]))
            elif ty == "ATTACK":
                atks.append((t["turn"], a.get("damage", 0), a.get("oppLife")))
    f["casts"], f["draws"], f["lands"], f["atks"] = casts, draws, lands, atks
    wt = f["win_turn"]
    f["kill_casts"] = [c for c in casts if c[0] == wt]
    f["kill_dmg"] = sum(d for (tn, d, _) in atks if tn == wt)
    f["lands_by_kill"] = len([1 for (tn, _) in lands if tn <= wt])
    f["draws_by_kill"] = len([1 for (tn, _) in draws if tn <= wt])
    f["draws_on_kill"] = len([1 for (tn, _) in draws if tn == wt])
    # reveals = every copy that reached the hand (kept opening hand + draws)
    rev = collections.Counter(f["open"]) + collections.Counter(n for (_, n) in draws)
    f["rev"] = rev
    f["cast_n"] = collections.Counter(n for (_, n, _) in casts)
    # Creatures in play at the end of each turn -- the magnet fan-out size, i.e. how many copies a
    # solo-target trick makes. Tokens are logged like any other permanent.
    f["creatures_after"] = {t["turn"]: sum(1 for c in t.get("boardAfter", {}).get("battlefield", [])
                                           if not c.get("isLand"))
                            for t in log["turns"]}
    # Hand size at the end of each turn. This is the ONLY visible trace of the cantrip riders: a
    # spell's own draw is not logged as a DRAW action (only the draw step is), so "cards drawn"
    # from the action list is just the turn count and says nothing. Hand size after a combo turn
    # does say something -- it is what is left after the chain both spent and refilled it.
    f["hand_after"] = {t["turn"]: len(t.get("boardAfter", {}).get("hand", [])) for t in log["turns"]}
    # Our own life total per turn. Fortifying Draught's +X/+X is X = life gained this turn, and
    # Oracle's Restoration banks +1 life PER RESOLVED COPY before Draught is cast -- so the life
    # curve is the direct, visible measure of how much Draught escalation the swap gave up.
    f["life_after"] = {t["turn"]: t.get("boardAfter", {}).get("playerLife") for t in log["turns"]}
    f["first_seen"] = {}
    for n in set(f["open"]):
        f["first_seen"][n] = 0
    for (tn, n) in draws:
        f["first_seen"].setdefault(n, tn)
    return f


def paid_x(mana_paid):
    """Libation is {X}{G}: every generic pip actually paid is X ({G} contributes nothing)."""
    return sum(int(t) for t in re.findall(r"\{([^}]*)\}", mana_paid) if t.isdigit())


base_i, arm_i = index_logs(os.path.join(GL, "base")), index_logs(os.path.join(GL, ARM))
div = json.load(open(os.path.join(ROOT, f"logs/mw_libation/{WHICH}_div.json")))
worse, better = set(div["worse"]), set(div["better"])

rows, bad = [], []
for gi in sorted(worse | better):
    s = SEED0 + gi
    if s not in base_i or s not in arm_i:
        continue
    B, A = feats(json.load(open(base_i[s]))), feats(json.load(open(arm_i[s])))
    # The repro must land on the batch's own win turn or the game is not the game we measured.
    if B["win_turn"] != div["base"][str(gi)] or A["win_turn"] != div["arm"][str(gi)]:
        bad.append(gi)
        continue
    lib_seen, lib_cast = A["rev"][LIB] > 0, A["cast_n"][LIB] > 0
    cut_extra_seen = B["rev"][CUT] - A["rev"][CUT]
    cut_extra_cast = B["cast_n"][CUT] - A["cast_n"][CUT]
    if B["mull_sig"] != A["mull_sig"]:
        cls = "MULLIGAN"
    elif not lib_seen and cut_extra_seen <= 0:
        cls = "SEARCH_ONLY"
    elif lib_cast and cut_extra_cast > 0:
        cls = "BOTH_CAST"
    elif not lib_cast and cut_extra_cast > 0:
        cls = "REPLACED_ONLY"
    elif lib_cast and cut_extra_cast <= 0:
        cls = "LIBATION_ONLY"
    else:
        cls = "NEITHER_CAST"
    rows.append({"gi": gi, "cls": cls, "dir": "worse" if gi in worse else "better",
                 "d": A["win_turn"] - B["win_turn"], "B": B, "A": A})

print(f"===== {WHICH}: base vs {ARM} =====")
print(f"divergent games with paired logs: {len(rows)}"
      + (f"   ({len(bad)} EXCLUDED: repro did not match the batch)" if bad else ""))
n = len(rows)
if not n:
    sys.exit("no classifiable games yet")

order = ["MULLIGAN", "SEARCH_ONLY", "NEITHER_CAST", "REPLACED_ONLY", "BOTH_CAST", "LIBATION_ONLY"]
tot = sum(r["d"] for r in rows)
print(f"\n{'class':<15}{'games':>7}{'%':>7}{'arm worse':>11}{'arm better':>12}"
      f"{'net turns':>11}{'share':>8}")
for c in order:
    g = [r for r in rows if r["cls"] == c]
    if not g:
        continue
    sd = sum(r["d"] for r in g)
    print(f"{c:<15}{len(g):>7}{100*len(g)/n:>6.1f}%{sum(1 for r in g if r['d']>0):>11}"
          f"{sum(1 for r in g if r['d']<0):>12}{sd:>+11}{100*sd/tot:>7.1f}%")
print(f"{'TOTAL':<15}{n:>7}{100.0:>6.1f}%{sum(1 for r in rows if r['d']>0):>11}"
      f"{sum(1 for r in rows if r['d']<0):>12}{tot:>+11}{100.0:>7.1f}%")
print(f"\nnet over the 20,000-game screen: {tot}/20000 = {tot/20000:+.4f} turns")

def both_cast_report(bc, title):
    print(f"\n--- BOTH_CAST {title} (n={len(bc)}) ---")
    if not bc:
        return
    xs = [paid_x(next(mp for (_, nm, mp) in r["A"]["casts"] if nm == LIB)) for r in bc]
    xs = [paid_x(next(mp for (_, nm, mp) in r["A"]["casts"] if nm == LIB)) for r in bc]
    print(f"  X paid for Libation: mean {st.mean(xs):.2f} median {st.median(xs)} "
          f"dist {dict(sorted(collections.Counter(xs).items()))}")
    lt = [next(t for (t, nm, _) in r["A"]["casts"] if nm == LIB) for r in bc]
    ct = [next(t for (t, nm, _) in r["B"]["casts"] if nm == CUT) for r in bc]
    print(f"  cast on turn -- Libation {dict(sorted(collections.Counter(lt).items()))}"
          f"  {CUT} {dict(sorted(collections.Counter(ct).items()))}")
    # Compare the two arms AT THE SAME TURN NUMBER, and specifically at the LAST turn both games
    # actually reached (min of the two win turns). Comparing at each arm's own kill turn flatters the
    # slower arm (more draw steps); comparing at the loser's kill turn reads zero for the arm that
    # had already won and stopped playing.
    T = [min(r["B"]["win_turn"], r["A"]["win_turn"]) for r in bc]
    hb = [r["B"]["hand_after"].get(t, 0) for r, t in zip(bc, T)]
    ha = [r["A"]["hand_after"].get(t, 0) for r, t in zip(bc, T)]
    print(f"  cards left in hand after the shared last turn: base {st.mean(hb):.2f} "
          f" arm {st.mean(ha):.2f}  ({st.mean(a - b for a, b in zip(ha, hb)):+.2f})")
    cr_b = [r["B"]["creatures_after"].get(t, 0) for r, t in zip(bc, T)]
    cr_a = [r["A"]["creatures_after"].get(t, 0) for r, t in zip(bc, T)]
    print(f"  creatures in play at the shared last turn: base {st.mean(cr_b):.2f}  arm {st.mean(cr_a):.2f}"
          f"  ({st.mean(a - b for a, b in zip(cr_a, cr_b)):+.2f})")
    sp_b = [len([1 for (tn, _, _) in r["B"]["casts"] if tn == t]) for r, t in zip(bc, T)]
    sp_a = [len([1 for (tn, _, _) in r["A"]["casts"] if tn == t]) for r, t in zip(bc, T)]
    print(f"  spells cast on the shared last turn: base {st.mean(sp_b):.2f}  arm {st.mean(sp_a):.2f}"
          f"  ({st.mean(a - b for a, b in zip(sp_a, sp_b)):+.2f})")
    dm_b = [sum(d for (tn, d, _) in r["B"]["atks"] if tn == t) for r, t in zip(bc, T)]
    dm_a = [sum(d for (tn, d, _) in r["A"]["atks"] if tn == t) for r, t in zip(bc, T)]
    print(f"  damage dealt on the shared last turn: base {st.mean(dm_b):.1f}  arm {st.mean(dm_a):.1f}"
          f"  ({st.mean(a - b for a, b in zip(dm_a, dm_b)):+.1f})")
    # THE CHAIN. Both cards are solo-target tricks the magnet copies once per other creature, so the
    # question "which card keeps the combo turn going" is answered by how long the chain that
    # CONTAINS it runs. Measured on each arm's OWN cast turn, so neither is credited for the other's
    # tempo: total spells cast that turn, and how many came AFTER the swapped card.
    def chain(rr, key, nm_want):
        t = next(t for (t, nm, _) in rr[key]["casts"] if nm == nm_want)
        seq = [nm for (tn, nm, _) in rr[key]["casts"] if tn == t]
        i = seq.index(nm_want)
        return t, len(seq), len(seq) - i - 1, rr[key]["creatures_after"].get(t, 0)
    cb_ = [chain(r, "B", CUT) for r in bc]
    ca_ = [chain(r, "A", LIB) for r in bc]
    print(f"  on the turn the swapped card was cast --")
    print(f"     spells cast that turn:      base {st.mean(x[1] for x in cb_):.2f} "
          f" arm {st.mean(x[1] for x in ca_):.2f}")
    print(f"     spells cast AFTER it:       base {st.mean(x[2] for x in cb_):.2f} "
          f" arm {st.mean(x[2] for x in ca_):.2f}")
    print(f"     creatures at end of it:     base {st.mean(x[3] for x in cb_):.2f} "
          f" arm {st.mean(x[3] for x in ca_):.2f}")
    lb = [r["B"]["life_after"].get(t) for r, t in zip(bc, T)]
    la = [r["A"]["life_after"].get(t) for r, t in zip(bc, T)]
    pair = [(x, y) for x, y in zip(lb, la) if x is not None and y is not None]
    if pair:
        print(f"  life total after the shared last turn: base {st.mean(x for x, _ in pair):.1f} "
              f" arm {st.mean(y for _, y in pair):.1f}"
              f"  ({st.mean(y - x for x, y in pair):+.1f})")
    # X=0 is a Libation cast purely for the 1/1 Citizen body: a +0/+0 "pump".
    z = [r for r, x in zip(bc, xs) if x == 0]
    print(f"  Libation cast for X=0 (a +0/+0 -- the body only): {len(z)}/{len(bc)} "
          f"({100*len(z)/len(bc):.0f}%)")
    onkill = sum(1 for r in bc if any(nm == LIB and tn == r["A"]["win_turn"]
                                      for (tn, nm, _) in r["A"]["casts"]))
    print(f"  Libation cast ON the arm's own kill turn: {onkill}/{len(bc)}")
    # Does the cut card's rider feed a payoff on the same turn it is cast?
    for payoff in ("Fists of Flame", "Fortifying Draught"):
        co = sum(1 for r in bc
                 if any(nm == payoff and tn == next(t for (t, m, _) in r["B"]["casts"] if m == CUT)
                        for (tn, nm, _) in r["B"]["casts"]))
        print(f"  base cast {payoff} on the same turn as {CUT}: {co}/{len(bc)} "
              f"({100*co/len(bc):.0f}%)")


bc_all = [r for r in rows if r["cls"] == "BOTH_CAST"]
# Split by direction. A pooled "base vs arm at base's kill turn" comparison is partly circular --
# on the games the arm loses, base has by definition already gone off and the arm has not -- so the
# same measurements are reported separately for the games the arm WINS, where the circularity runs
# the other way and the two together bracket the effect.
both_cast_report([r for r in bc_all if r["d"] > 0], "-- games the ARM LOSES")
both_cast_report([r for r in bc_all if r["d"] < 0], "-- games the ARM WINS")

print("\n--- REPLACED_ONLY: base cast its card, the arm never cast Libation ---")
ro = [r for r in rows if r["cls"] == "REPLACED_ONLY"]
if ro:
    print(f"  n={len(ro)}  ({sum(1 for r in ro if r['d']>0)} arm worse / "
          f"{sum(1 for r in ro if r['d']<0)} arm better)")
    tn = [next(t for (t, nm, _) in r["B"]["casts"] if nm == CUT) for r in ro]
    print(f"  base cast {CUT} on turn: {dict(sorted(collections.Counter(tn).items()))}")
    seen = [r["A"]["first_seen"].get(LIB) for r in ro]
    print(f"  arm first held Libation on turn: "
          f"{dict(sorted(collections.Counter(x for x in seen if x is not None).items()))}"
          f"  (never held: {sum(1 for x in seen if x is None)})")
    print(f"  lands by kill turn -- arm {st.mean([r['A']['lands_by_kill'] for r in ro]):.2f}"
          f"  base {st.mean([r['B']['lands_by_kill'] for r in ro]):.2f}")

print("\n--- the kill turn, base vs arm, over every divergent game ---")
cb, ca = collections.Counter(), collections.Counter()
for r in rows:
    for c in r["B"]["kill_casts"]:
        cb[c[1]] += 1
    for c in r["A"]["kill_casts"]:
        ca[c[1]] += 1
print(f"  {'card cast on the kill turn':<28}{'base':>7}{'arm':>7}{'delta':>8}")
for k in sorted(set(cb) | set(ca), key=lambda k: -(cb[k] + ca[k])):
    print(f"  {k:<28}{cb[k]:>7}{ca[k]:>7}{ca[k]-cb[k]:>+8}")
print(f"  {'spells cast on kill turn':<28}{sum(cb.values()):>7}{sum(ca.values()):>7}"
      f"{sum(ca.values())-sum(cb.values()):>+8}")
print(f"  {'cards drawn on kill turn':<28}"
      f"{sum(r['B']['draws_on_kill'] for r in rows):>7}"
      f"{sum(r['A']['draws_on_kill'] for r in rows):>7}"
      f"{sum(r['A']['draws_on_kill']-r['B']['draws_on_kill'] for r in rows):>+8}")

# Per-game filing: one line per divergent game, with the facts the class rests on, so any single
# claim in the summary can be traced back to the games that produced it.
with open(os.path.join(ROOT, f"logs/mw_libation/{WHICH}_games.txt"), "w") as fh:
    fh.write(f"# {WHICH}: every divergent game, filed under its mechanism class\n"
             f"# gi seed base->arm class  X(Libation)  spells_on_shared_last_turn(base/arm)"
             f"  hand_after(base/arm)  creatures(base/arm)\n")
    for r in sorted(rows, key=lambda r: (r["cls"], -r["d"], r["gi"])):
        t = min(r["B"]["win_turn"], r["A"]["win_turn"])
        x = next((paid_x(mp) for (_, nm, mp) in r["A"]["casts"] if nm == LIB), None)
        sb = len([1 for (tn, _, _) in r["B"]["casts"] if tn == t])
        sa = len([1 for (tn, _, _) in r["A"]["casts"] if tn == t])
        fh.write(f"{r['gi']:>6} {SEED0+r['gi']:>8} T{r['B']['win_turn']}->T{r['A']['win_turn']} "
                 f"{r['d']:+d} {r['cls']:<14} X={x if x is not None else '-':<3} "
                 f"spells={sb}/{sa} hand={r['B']['hand_after'].get(t,0)}/{r['A']['hand_after'].get(t,0)} "
                 f"cr={r['B']['creatures_after'].get(t,0)}/{r['A']['creatures_after'].get(t,0)}\n")

json.dump({"which": WHICH, "excluded_repro_mismatch": bad,
           "rows": [{k: v for k, v in r.items() if k in ("gi", "cls", "dir", "d")} for r in rows]},
          open(os.path.join(ROOT, f"logs/mw_libation/{WHICH}_classified.json"), "w"), indent=0)

if DUMP:
    print("\n--- exemplars ---")
    for c in order:
        for r in [r for r in rows if r["cls"] == c][:DUMP]:
            print(f"\n[{c}] gi={r['gi']} seed={SEED0+r['gi']}  base T{r['B']['win_turn']}"
                  f" -> arm T{r['A']['win_turn']}")
            for tag, F in (("base", r["B"]), ("arm ", r["A"])):
                print(f"   {tag} mull={F['mulls']} kill_dmg={F['kill_dmg']}  " +
                      " ".join(f"T{t}:{nm}{'('+mp+')' if nm == LIB else ''}"
                               for (t, nm, mp) in F["casts"]))
