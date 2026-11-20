"""Flat diff of two JSON files (leaf paths whose values differ). Usage: jdiff.py old new"""
import json, sys


def flat(x, p=''):
    if isinstance(x, dict):
        for k, v in x.items():
            yield from flat(v, f'{p}.{k}')
    elif isinstance(x, list) and len(x) < 40 and not all(isinstance(e, (int, float)) for e in x):
        for i, v in enumerate(x):
            yield from flat(v, f'{p}[{i}]')
    else:
        yield p, x


a = dict(flat(json.load(open(sys.argv[1]))))
b = dict(flat(json.load(open(sys.argv[2]))))
for k in sorted(set(a) | set(b)):
    if a.get(k, '<absent>') != b.get(k, '<absent>'):
        print(k, ':', str(a.get(k, '<absent>'))[:120], '->', str(b.get(k, '<absent>'))[:120])
