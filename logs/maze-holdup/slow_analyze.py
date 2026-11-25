#!/usr/bin/env python3
"""slow_analyze.py <ab.out> <slow out files...>: per slower game, the play-setting turns (off / on), d8 b0 (off / on)
and play with MTG_LINE_KEEP_CRIT=0 (off / on). RECOVERS: on at d8 b0 <= off at play. LKR-TRACED: the ON-vs-OFF gap at
play closes when the line-keep rung is off in both arms (on_lk0 <= off_lk0)."""
import re, sys, collections
R = collections.defaultdict(dict)
def rd(p):
    for l in open(p):
        m = re.match(r'maze_(\w+?)_g(\d+)_(?:(d8|lk0|nc)_)?(on|off): played=\d+ avg=(\S+)', l)
        if m:
            wt = float(m.group(5)); wt = 99.0 if wt <= 0 else wt
            R[(m.group(1), int(m.group(2)))][(m.group(3) or "play") + "_" + m.group(4)] = wt
rd(sys.argv[1])
for p in sys.argv[2:]: rd(p)
for (fmt, gi), r in sorted(R.items()):
    if "play_on" not in r or "play_off" not in r or r["play_on"] <= r["play_off"]: continue
    g = lambda k: f"{r[k]:.0f}" if k in r else "-"
    rec = "d8_on" in r and r["d8_on"] <= r["play_off"]
    lkr = "lk0_on" in r and "lk0_off" in r and r["lk0_on"] <= r["lk0_off"]
    nc = f" nc off/on={g('nc_off')}/{g('nc_on')}" if "nc_on" in r else ""
    print(f"{fmt} gi={gi}: play off/on={g('play_off')}/{g('play_on')} d8 off/on={g('d8_off')}/{g('d8_on')} "
          f"lk0 off/on={g('lk0_off')}/{g('lk0_on')}{nc}  {'RECOVERS' if rec else 'NOT-RECOVERED'}"
          f"{'  LKR-gap-closes' if lkr else ''}")
