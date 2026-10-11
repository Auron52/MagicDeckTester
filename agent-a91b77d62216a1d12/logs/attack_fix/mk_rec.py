#!/usr/bin/env python3
"""mk_rec.py <outdir>: recovery manifest -- every game x {on, off, each lever off, each lever alone}."""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from mkspecs import NEW, SHORT
HERE = os.path.dirname(os.path.abspath(__file__))
args = ['python3', os.path.join(HERE, 'recover.py'), 'mk', sys.argv[1], 'on=-', 'off=' + ','.join(NEW)]
for l in NEW:
    args.append(SHORT[l] + '=' + l)
for l in NEW:
    args.append('only' + SHORT[l][2:] + '=' + ','.join(x for x in NEW if x != l))
subprocess.check_call(args)
