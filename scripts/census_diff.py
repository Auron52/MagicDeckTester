#!/usr/bin/env python3
"""Paired diff of two MTG_TURN_CENSUS runs over the SAME seed span.

  python3 logs/turncensus/censusdiff.py <base_dir> <new_dir>

Why paired and why same seeds: a census row is one search decision, and a lever that changes play
changes WHICH decisions exist -- so row-for-row joining is wrong (the turn-3 decision of a game the
lever won a turn earlier has no twin). What IS comparable is the per-seed total: the same 40 games
from the same deck, played to completion, costing N units. So this sums every counter per seed and
reports (a) the pooled ratio, (b) the per-seed sign count, which is the scheduling-immune statistic
(`units` is deterministic; see memory pooled-ab-needs-arms-innermost).

`contended` rows are excluded exactly as turn_census.py excludes them.
"""
import collections
import os
import sys

# counters worth reading as a ratio, grouped for the report
GROUPS = [
    ('WORK', ['units', 'wall_us', 'cand_scored']),
    ('PER-SITE UNITS', ['u_la_cand', 'u_rollout_step', 'u_greedy_fallback', 'u_greedy_walk',
                        'u_la_bp_wave', 'u_fs_pre', 'u_fs_bp_wave', 'u_fs_bp_node']),
    ('SUBSET FUNNEL', ['sub_entered', 'sub_passed', 'sub_rej_dupsrc', 'sub_rej_dupsrc_s1',
                       'dupc_fold_prefix']),
    ('PLAN SPACE', ['space_odo', 'space_plans', 'space_dedup', 'walk_calls', 'walk_groups']),
    ('WALK FUNNEL', ['walk_enter', 'walk_pass_rules', 'walk_pass_mana', 'walk_pass_payable',
                     'walk_pass_color', 'walk_scored']),
    ('ENUM / MEMO', ['enum_enter', 'enum_emitted', 'enum_hits', 'enum_misses']),
    ('DOWNSTREAM', ['rollout_calls', 'rollout_steps', 'pay_calls', 'act_fired']),
    ('SUBSET OPTIMISM', ['drop_tapped', 'drop_unpaid', 'hold_mask', 'hold_solo', 'hold_retry']),
    ('RE-WORK', ['idwaste_units', 'idwaste_rescuable', 'lad_warm_units', 'lad_commit_units']),
    ('CONDEMNATION', ['bp_condemn_seen', 'bp_condemn_drops', 'newonly_seen', 'newonly_dropped']),
]


def load(d):
    """-> (per-seed dict of counter sums, row count, overrun histogram)"""
    per_seed = collections.defaultdict(lambda: collections.Counter())
    rows, over = 0, collections.Counter()
    for fn in sorted(os.listdir(d)):
        if not fn.endswith('.tsv'):
            continue
        with open(os.path.join(d, fn)) as f:
            hdr = None
            for ln in f:
                if ln.startswith('#'):
                    continue
                cells = ln.rstrip('\n').split('\t')
                if hdr is None:
                    hdr = cells
                    ix = {c: i for i, c in enumerate(hdr)}
                    continue
                if cells[ix['contended']] != '0':
                    continue
                rows += 1
                seed = cells[ix['seed']]
                acc = per_seed[seed]
                for c, i in ix.items():
                    if c in ('seed', 'skey', 'contended'):
                        continue
                    try:
                        acc[c] += int(cells[i])
                    except (ValueError, IndexError):
                        pass
                acc['rows'] += 1
                u, b = int(cells[ix['units']]), int(cells[ix['budget']])
                if b > 0:
                    # the census's budget column is virtual-us; units-vs-budget ladder as in summary
                    for mult in (1, 2, 5, 10, 25):
                        if u * 1000 > b * mult:
                            over[mult] += 1
    return per_seed, rows, over


def main():
    base_d, new_d = sys.argv[1], sys.argv[2]
    b, brows, bover = load(base_d)
    n, nrows, nover = load(new_d)
    shared = sorted(set(b) & set(n))
    print(f'base {base_d}: {brows} rows, {len(b)} seeds')
    print(f'new  {new_d}: {nrows} rows, {len(n)} seeds')
    print(f'shared seeds: {len(shared)}   (rows {nrows}/{brows} = '
          f'{nrows / max(brows, 1):.3f} -- decisions, not work)')
    if len(shared) < max(len(b), len(n)):
        print('  !! seed spans differ -- only shared seeds are compared')

    def tot(src, col):
        return sum(src[s][col] for s in shared)

    for title, cols in GROUPS:
        print(f'\n{title}')
        print(f"  {'counter':24s}{'base':>16s}{'new':>16s}{'ratio':>9s}{'per-seed down/up':>19s}")
        for c in cols:
            bt, nt = tot(b, c), tot(n, c)
            if bt == 0 and nt == 0:
                continue
            down = sum(1 for s in shared if n[s][c] < b[s][c])
            up = sum(1 for s in shared if n[s][c] > b[s][c])
            r = (nt / bt) if bt else float('inf')
            print(f'  {c:24s}{bt:16,d}{nt:16,d}{r:9.3f}{f"{down}/{up}":>19s}')

    print('\nUNITS, per-seed paired (the scheduling-immune statistic)')
    ratios = sorted(n[s]['units'] / b[s]['units'] for s in shared if b[s]['units'])
    if ratios:
        mid = ratios[len(ratios) // 2]
        print(f'  pooled {tot(n, "units") / tot(b, "units"):.4f}   median {mid:.4f}   '
              f'min {ratios[0]:.4f}   max {ratios[-1]:.4f}   '
              f'down on {sum(1 for r in ratios if r < 1)}/{len(ratios)} seeds')

    print('\nBUDGET DISCIPLINE (decisions over Nx budget)')
    for mult in (1, 2, 5, 10, 25):
        print(f'  > {mult:2d}x: {bover[mult]:6,d} -> {nover[mult]:6,d}')

    print('\nDERIVED')
    for lbl, num, den in [('units / candidate scored', 'units', 'cand_scored'),
                          ('subset pass rate', 'sub_passed', 'sub_entered'),
                          ('fold rejections / entered', 'dupc_fold_prefix', 'sub_entered'),
                          ('plans / odometer', 'space_plans', 'space_odo'),
                          ('pay solves / unit', 'pay_calls', 'units'),
                          ('rollout steps / unit', 'rollout_steps', 'units'),
                          ('memo hit rate', 'enum_hits', 'enum_enter'),
                          ('dropped casts / candidate',
                           'drop_unpaid', 'cand_scored')]:
        bv = tot(b, num) / tot(b, den) if tot(b, den) else 0
        nv = tot(n, num) / tot(n, den) if tot(n, den) else 0
        print(f'  {lbl:28s}{bv:12.4f} -> {nv:12.4f}')


if __name__ == '__main__':
    main()
