#!/usr/bin/env python3
# Turn-regret A/B summary over a keepmodel_overnight harness output dir (post-processes the dumped
# wins_<tag>_d<d>_s<s>.wins files -- no re-run). TURN-REGRET = per game win_turn if won else loss
# (= max_turns+1, default 9); LOWER is better. This is the user's objective (NOT win-count).
#
# Usage: python3 test/keepmodel_turnregret.py <out_dir> <base_tag> <cmp_tag> [loss] [depths] [seeds]
#   e.g. python3 test/keepmodel_turnregret.py logs/keepmodel_overnight/Anti-Lifegain/policy_h12000 \
#                committed km0.02 9 "0 3 5" "4004 5005 6006 7007"
import sys, os

out = sys.argv[1]
base = sys.argv[2]
cmp_ = sys.argv[3]
loss = int(sys.argv[4]) if len(sys.argv) > 4 else 9
depths = (sys.argv[5].split() if len(sys.argv) > 5 else ["0", "3", "5"])
seeds = (sys.argv[6].split() if len(sys.argv) > 6 else ["4004", "5005", "6006", "7007"])

def load(fn):
    m = {}
    if not os.path.exists(fn): return m
    for ln in open(fn):
        a = ln.split()
        if len(a) >= 2: m[int(a[0])] = int(a[1])
    return m

def reg(m, gis): return sum((m[g] if m[g] > 0 else loss) for g in gis) / len(gis) if gis else 0
def won(m, gis): return sum(1 for g in gis if m[g] > 0)

print(f"== turn-regret A/B  {base} vs {cmp_}  (loss={loss}; lower=better) ==")
for d in depths:
    tot_b = tot_c = n_tot = wl_tot = lw_tot = 0
    for s in seeds:
        b = load(os.path.join(out, f"wins_{base}_d{d}_s{s}.wins"))
        c = load(os.path.join(out, f"wins_{cmp_}_d{d}_s{s}.wins"))
        gis = sorted(set(b) & set(c))
        if not gis: continue
        rb, rc = reg(b, gis), reg(c, gis)
        wl = sum(1 for g in gis if b[g] > 0 and c[g] < 0)
        lw = sum(1 for g in gis if b[g] < 0 and c[g] > 0)
        tot_b += rb * len(gis); tot_c += rc * len(gis); n_tot += len(gis)
        wl_tot += wl; lw_tot += lw
        print(f"  d{d} s{s}: regret {rb:.4f} -> {rc:.4f} ({rc-rb:+.4f}) | won {won(b,gis)}->{won(c,gis)} | W->L={wl} L->W={lw}")
    if n_tot:
        print(f"  d{d} ALL : regret {tot_b/n_tot:.4f} -> {tot_c/n_tot:.4f} ({(tot_c-tot_b)/n_tot:+.4f}) | W->L={wl_tot} L->W={lw_tot}  [n={n_tot}]")
