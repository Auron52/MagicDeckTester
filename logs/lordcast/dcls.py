#!/usr/bin/env python3
"""dcls.py <dir> turn | report <label>  (helper of dcls.sh; dir holds a/ (off) and b/ (onB) game logs, pa.err/pb.err).
turn: the first turn whose actions differ. report: one line -- divergence turn, each arm's m1 casts, opp life before,
dealt that turn, win turns, and for each arm's cast set its [proj] read in BOTH arms' traces (proj_atk, wins)."""
import glob, json, re, sys
O, mode = sys.argv[1], sys.argv[2]
def load(x):
    f = glob.glob(f"{O}/{x}/*.json")
    return json.load(open(f[0])) if f else None
A, B = load("a"), load("b")
def turns(g):
    d = {}
    for t in g["turns"]: d.setdefault(t["turn"], []).append(t)
    return d
TA, TB = turns(A), turns(B)
def acts(ts): return [(t["phase"], [(a.get("type"), a.get("cardName")) for a in t["actions"]]) for t in ts]
first = None
for T in sorted(set(TA) & set(TB)):
    if acts(TA[T]) != acts(TB[T]): first = T; break
if mode == "turn":
    print(first or ""); sys.exit()
T = first
def m1casts(ts): return sorted(a.get("cardName") for t in ts if t["phase"] == "MAIN_1" for a in t["actions"]
                               if a.get("type") == "CAST_SPELL")
def life_before(g, T):
    prev = 20
    for t in g["turns"]:
        if t["turn"] >= T: break
        prev = t["boardAfter"].get("opponentLife", prev)
    return prev
def dealt(ts, before):
    end = before
    for t in ts: end = t["boardAfter"].get("opponentLife", end)
    return before - end
proj = re.compile(r"^\[proj\] T(\d+) m1 \{(.*?)\} .*?proj_atk=(-?\d+).*? opp=(\d+) wins=(\d)")
def reads(err, casts):
    out = []
    for ln in open(err, errors="replace"):
        m = proj.match(ln)
        if not m or int(m.group(1)) != T: continue
        names = sorted(re.sub(r"(\(free\))?(\+RM)?#e-?\d+$", "", x) for x in m.group(2).split(";") if x)
        if names == casts: out.append((int(m.group(3)), int(m.group(5))))
    if not out: return "none"
    return f"{max(p for p, _ in out)}/{'W' if any(w for _, w in out) else '-'}"
ca, cb = m1casts(TA[T]), m1casts(TB[T])
lb = life_before(A, T)
wa, wb = A["result"].get("turn"), B["result"].get("turn")
print(f"{sys.argv[3]} T{T} opp={lb} win {wa}->{wb} | off casts={ca} dealt={dealt(TA[T], lb)} read(on)={reads(O+'/pb.err', ca)}"
      f" read(off)={reads(O+'/pa.err', ca)} | on casts={cb} dealt={dealt(TB[T], lb)} read(on)={reads(O+'/pb.err', cb)}"
      f" read(off)={reads(O+'/pa.err', cb)}")
