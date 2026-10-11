#!/usr/bin/env python3
"""Pooled A/B tooling for the attack-projection fixes. ONE manifest per run (never a loop).

  ab.py mk <outdir> <spec.json>      write <outdir>/m.json (+ spec copy)
  ab.py report <outdir> [ref_arm]    paired per-game: every arm vs ref (default 'on'), per deck x cell
  ab.py digests <outA> <outB>        compare "<name>: played= avg= digest=" lines of two stdout files

spec.json:
  {"decks": {"fivecolour": {"seed_d0": 5100001, "seed_d3": 5160001}, ...},
   "cells": {"d0": {"games": 3000, "chunk": 250}, "d3": {"games": 300, "chunk": 25}},
   "arms":  {"on": [], "off": ["MTG_X", ...], "noX": ["MTG_X"]},
   "deck_arms": {"fivecolour": ["on", "off", "noX"], ...},     # optional: arms per deck (default all)
   "deck_cells": {"burn": ["d0"]}}                             # optional: cells per deck (default all)
Arm flags are forced OFF (false). An arm may instead be {"off": [...], "on": [...]}.
"""
import json, os, re, sys

DECKS = {
    'fivecolour': 'decks/FiveColour/FiveColour', 'stompy': 'decks/StompySurprise/StompySurprise',
    'selesnya': 'decks/SelesnyaLifegain/SelesnyaLifegain', 'bruna': 'decks/Bruna/Bruna',
    'snow': 'decks/Snow/Snow', 'burn': 'decks/burn/burn', 'dragonstorm': 'decks/Dragonstorm/Dragonstorm',
    'hinata': 'decks/Hinata2/Hinata2', 'critter': 'decks/CritterLifegain/CritterLifegain',
    'angels': 'decks/Angels/Angels', 'giants': 'decks/Giants/Giants', 'goblins': 'decks/Goblins/Goblins',
    'pirates': 'decks/Pirates/Pirates', 'kitty': 'decks/KittyEquipment/KittyEquipment',
    'knights': 'decks/Knights/Knights', 'whiteknights': 'decks/WhiteKnights/WhiteKnights',
    'antilife': 'decks/Anti-Lifegain/Anti-Lifegain', 'auras': 'decks/Auras/Auras',
    'creature_giving': 'decks/Creature Giving/Creature Giving', 'mirrorwing': 'decks/Mirrorwing Dragon/Mirrorwing Dragon',
    'minotaur': 'decks/Minotaur/Minotaur', 'dragons': 'decks/Dragons/Dragons',
    'breaching': 'decks/BreachingDragonstorm/BreachingDragonstorm', 'melira': 'decks/Melira Pod/Melira Pod',
    'fluctuator': 'decks/Fluctuator/Fluctuator', 'soldiers': 'decks/Soldiers/soldiers',
    'pd': 'decks/Prevent Damage/Prevent Damage', 'fungus': 'decks/Fungus/Fungus',
    'slivers': 'decks/slivers_vial/slivers_vial', 'th': 'decks/treasure_hunt/treasure_hunt',
}
EXT = {'burn': '.txt', 'slivers': '.txt', 'th': '.txt'}


def deck_paths(k):
    base = DECKS[k]
    deck = base + EXT.get(k, '.cod')
    prof = base + '.profile.json'
    if k == 'soldiers':
        prof = 'decks/Soldiers/soldiers.profile.json'
    return deck, prof


def cell_fields(cell):
    if cell == 'd0':
        return {'depth': 0, 'budget_ms': 0, 'ignore_play_profile': True}
    if cell == 'd3':
        return {'depth': 3, 'budget_ms': 10, 'ignore_play_profile': True}
    if cell == 'ship':
        return {'budget_ms': 20}
    raise SystemExit('bad cell')


def mk(outdir, specfn):
    spec = json.load(open(specfn))
    os.makedirs(outdir, exist_ok=True)
    json.dump(spec, open(os.path.join(outdir, 'spec.json'), 'w'), indent=1)
    jobs = []
    for deck, dsp in spec['decks'].items():
        dpath, ppath = deck_paths(deck)
        arms = spec.get('deck_arms', {}).get(deck, list(spec['arms'].keys()))
        cells = spec.get('deck_cells', {}).get(deck, list(spec['cells'].keys()))
        for cell in cells:
            arms = spec.get('cell_arms', {}).get(deck, {}).get(cell, spec.get('deck_arms', {}).get(deck, list(spec['arms'].keys())))
            c = spec['cells'][cell]
            seed0 = dsp['seed_' + cell]
            for arm in arms:
                a = spec['arms'][arm]
                flags = {}
                if isinstance(a, dict):
                    flags.update({l: False for l in a.get('off', [])})
                    flags.update({l: True for l in a.get('on', [])})
                else:
                    flags.update({l: False for l in a})
                for off in range(0, c['games'], c['chunk']):
                    n = min(c['chunk'], c['games'] - off)
                    j = {'name': '%s_%s_%s.c%d' % (deck, cell, arm, off), 'deck': dpath, 'profile': ppath,
                         'games': n, 'seed': seed0 + off, 'game_index': off}
                    j.update(cell_fields(cell))
                    if flags:
                        j['flags'] = flags
                    jobs.append(j)
    # Long cells first helps the LPT scheduler; the runner also sorts, but keep d3 ahead anyway.
    jobs.sort(key=lambda j: 0 if j.get('depth', 0) > 0 or 'depth' not in j else 1)
    json.dump({'jobs': jobs}, open(os.path.join(outdir, 'm.json'), 'w'))
    print('%d jobs, %d games -> %s' % (len(jobs), sum(j['games'] for j in jobs), os.path.join(outdir, 'm.json')))


def load_wins(outdir, mt=8):
    wt = {}
    for ln in open(os.path.join(outdir, 'err'), errors='replace'):
        m = re.search(r'\[win\] job=(\S+)\.c\d+ gi=(\d+) wt=(-?\d+)', ln)
        if not m:
            continue
        w = int(m.group(3))
        wt.setdefault(m.group(1), {})[int(m.group(2))] = w if w > 0 else mt + 1
    return wt


def report(outdir, ref='on'):
    spec = json.load(open(os.path.join(outdir, 'spec.json')))
    wt = load_wins(outdir)
    for deck in spec['decks']:
        arms = spec.get('deck_arms', {}).get(deck, list(spec['arms'].keys()))
        cells = spec.get('deck_cells', {}).get(deck, list(spec['cells'].keys()))
        for cell in cells:
            arms = spec.get('cell_arms', {}).get(deck, {}).get(cell, spec.get('deck_arms', {}).get(deck, list(spec['arms'].keys())))
            R = wt.get('%s_%s_%s' % (deck, cell, ref), {})
            if not R:
                print('%-11s %-3s  (no %s games)' % (deck, cell, ref))
                continue
            print('%-11s %-3s  %s: n=%d avg=%.4f' % (deck, cell, ref, len(R), sum(R.values()) / len(R)))
            for arm in arms:
                if arm == ref:
                    continue
                A = wt.get('%s_%s_%s' % (deck, cell, arm), {})
                common = sorted(set(R) & set(A))
                if not common:
                    print('    %-22s (no games)' % arm)
                    continue
                net = sum(R[g] - A[g] for g in common)          # ref minus arm: <0 = ref faster
                fast = [g for g in common if R[g] < A[g]]
                slow = [g for g in common if R[g] > A[g]]
                print('    %s vs %-22s n=%5d  net(%s-%s)=%+5d  %s faster=%4d slower=%4d   slower: %s' % (
                    ref, arm, len(common), ref, arm, net, ref, len(fast), len(slow),
                    ' '.join('%d:%d->%d' % (g, A[g], R[g]) for g in slow[:12])))


def digests(a, b):
    def load(fn):
        d = {}
        for ln in open(fn, errors='replace'):
            m = re.match(r'(\S+): played=(\d+) avg=(\S+) digest=(\S+)', ln)
            if m:
                d[m.group(1)] = (m.group(2), m.group(3), m.group(4))
        return d
    A, B = load(a), load(b)
    same = diff = 0
    for k in sorted(set(A) | set(B)):
        if A.get(k) == B.get(k):
            same += 1
        else:
            diff += 1
            print('DIFF %-40s %s | %s' % (k, A.get(k), B.get(k)))
    print('identical=%d differing=%d' % (same, diff))


def census(outdir, ref='on'):
    """Per deck x cell x arm: how many chunks' digests differ from the ref arm's (play changed at all)."""
    dig = {}
    for ln in open(os.path.join(outdir, 'out'), errors='replace'):
        m = re.match(r'(\S+)_(d0|d3|ship)_(\S+)\.c(\d+): played=(\d+) avg=(\S+) digest=(\S+)', ln)
        if m:
            dig[(m.group(1), m.group(2), m.group(3), int(m.group(4)))] = (m.group(6), m.group(7))
    spec = json.load(open(os.path.join(outdir, 'spec.json')))
    wt = load_wins(outdir)
    for deck in spec['decks']:
        arms = spec.get('deck_arms', {}).get(deck, list(spec['arms'].keys()))
        cells = spec.get('deck_cells', {}).get(deck, list(spec['cells'].keys()))
        for cell in cells:
            arms = spec.get('cell_arms', {}).get(deck, {}).get(cell, spec.get('deck_arms', {}).get(deck, list(spec['arms'].keys())))
            row = []
            for arm in arms:
                if arm == ref:
                    continue
                keys = [k for k in dig if k[0] == deck and k[1] == cell and k[2] == arm]
                nd = sum(1 for k in keys if dig.get((deck, cell, ref, k[3])) != dig[k])
                R = wt.get('%s_%s_%s' % (deck, cell, ref), {})
                A = wt.get('%s_%s_%s' % (deck, cell, arm), {})
                common = set(R) & set(A)
                net = sum(R[g] - A[g] for g in common)
                ng = sum(1 for g in common if R[g] != A[g])
                row.append('%s:%d/%d chunks, %d games, net%+d' % (arm, nd, len(keys), ng, net))
            print('%-15s %-3s %s' % (deck, cell, '  '.join(row)))


if __name__ == '__main__':
    cmd = sys.argv[1]
    if cmd == 'census':
        census(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else 'on')
        sys.exit(0)
    if cmd == 'mk':
        mk(sys.argv[2], sys.argv[3])
    elif cmd == 'report':
        report(sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else 'on')
    elif cmd == 'digests':
        digests(sys.argv[2], sys.argv[3])
