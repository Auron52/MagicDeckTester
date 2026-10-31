#!/usr/bin/env python3
"""Read Cyclone validation results, fail incomplete data, emit unrun repros.

quality: paired per-game reports for QC/cast/payment/nested (prunes has its own
reporter), FD/nonconv inventory, ordinary-world diagnostic and slower-game
recovery manifests. memory: exact wins/digests/units versus the stored primary worker count.
memory-preflight: interim normal t1/t6 overlap; holds for review if FSL binds.
This script never starts the engine or substitutes missing outcomes.
"""
import argparse
import collections
import copy
import hashlib
import importlib.util
import json
import math
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
BASE = next(p for p in HERE.parents
            if (p / 'CLAUDE.md').is_file() and (p / '.git').is_dir())
DEFAULT = BASE / 'build/scratch/cyclone-finish/v4/final-pgo-inputs'
_spec = importlib.util.spec_from_file_location('cyclone_protocol', HERE / 'prepared_protocol.py')
protocol = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(protocol)


def read_json(path):
    return json.loads(Path(path).read_text())


def write_json(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + '\n')


def table(path, width):
    if not path.is_file():
        raise ValueError('missing ' + str(path))
    rows = {}
    for n, line in enumerate(path.read_text().splitlines(), 1):
        if not line.strip() or line.startswith('#'):
            continue
        fields = line.split()
        if len(fields) != width:
            raise ValueError(f'{path}:{n}: expected{width} fields, got{len(fields)}')
        gi = int(fields[0])
        if gi in rows:
            raise ValueError(f'{path}:{n}: duplicate game index{gi}')
        rows[gi] = fields[1:]
    return rows


def read_job(job, wins):
    name = job['name']
    w, u = table(wins / (name + '.wins'), 3), table(wins / (name + '.units'), 2)
    start, count = job.get('game_index', 0), job['games']
    expected = set(range(start, start + count))
    if set(w) != expected or set(u) != expected:
        raise ValueError(f'{name}: incomplete/misindexed wins or units: expected{count}, wins{len(w)}, units{len(u)}')
    result = {}
    for gi in sorted(expected):
        wt, digest = int(w[gi][0]), w[gi][1].lower()
        units = int(u[gi][0])
        horizon = job.get('max_turns', 8)
        if wt > horizon or wt < -1 or units < 0 or not re.fullmatch('[0-9a-f]{16}', digest):
            raise ValueError(f'{name}: invalid win/digest/units at game{gi}')
        seed = job['seed'] + gi - start
        result[(seed, gi)] = dict(seed=seed, gi=gi, win=wt, score=wt if wt > 0 else horizon + 1,
                                 digest=digest, units=units, job_name=name, job=job)
    return result


def load_cells(jobs, wins, maps=None):
    data, expected, specs, problems = {}, collections.Counter(), {}, []
    for job in jobs:
        name = job['name']
        cell = maps[name]['logical_cell'] if maps else re.sub(r'_off\d+$', '', name)
        expected[cell] += job['games']
        specs.setdefault(cell, []).append(job)
        data.setdefault(cell, {})
        try:
            rows = read_job(job, wins)
            overlap = set(data[cell]) & set(rows)
            if overlap:
                raise ValueError(f'{cell}: duplicate physical identities{sorted(overlap)[:3]}')
            data[cell].update(rows)
        except (OSError, ValueError) as error:
            problems.append(dict(job=name, cell=cell, error=str(error)))
    return dict(data=data, expected=expected, specs=specs, problems=problems)


def aggregate(rows, expected):
    n = len(rows)
    return dict(n=n, expected=expected, complete=n == expected,
                avg_win_turn=sum(r['score'] for r in rows.values()) / n if n else None,
                mean_units=sum(r['units'] for r in rows.values()) / n if n else None,
                total_units=sum(r['units'] for r in rows.values()),
                note=None if n == expected else 'Partial completed jobs only; no adoption verdict.')


def paired(old, new, expected):
    keys = sorted(set(old) & set(new))
    n = len(keys)
    deltas = [new[k]['score'] - old[k]['score'] for k in keys]
    mean = sum(deltas) / n if n else None
    se = math.sqrt(sum((d - mean) ** 2 for d in deltas) / (n - 1) / n) if n > 1 else None
    old_units, new_units = sum(old[k]['units'] for k in keys), sum(new[k]['units'] for k in keys)
    slower = []
    for key, delta in zip(keys, deltas):
        if delta > 0:
            a, b = old[key], new[key]
            slower.append(dict(seed=key[0], gi=key[1], old_win=a['win'], new_win=b['win'], delta=delta,
                               old_job=a['job_name'], new_job=b['job_name']))
    return dict(n=n, expected=expected, complete=n == expected,
                delta=mean, se=se, net_turns=sum(deltas),
                old_mean=sum(old[k]['score'] for k in keys) / n if n else None,
                new_mean=sum(new[k]['score'] for k in keys) / n if n else None,
                faster=sum(d < 0 for d in deltas), slower=len(slower), ties=sum(d == 0 for d in deltas),
                digest_changed=sum(old[k]['digest'] != new[k]['digest'] for k in keys),
                units_ratio=new_units / old_units if old_units else None,
                old_total_units=old_units, new_total_units=new_units,
                unpaired_old=len(set(old) - set(new)), unpaired_new=len(set(new) - set(old)),
                slower_games=slower)


class Repros:
    def __init__(self):
        self.jobs, self.reasons = {}, {}

    def add(self, record, reason, depth=None, multiplier=None, actual_world=False):
        job = copy.deepcopy(record['job'])
        job.update(seed=record['seed'], game_index=record['gi'], games=1)
        job.pop('weight', None)
        if depth is not None:
            job.update(depth=depth, budget_ms=0, ignore_play_profile=True)
        if multiplier is not None:
            job['budget_ms'] *= multiplier
        if actual_world:
            job.pop('blind_salt', None)
        job.pop('name')
        key = hashlib.sha256(json.dumps(job, sort_keys=True).encode()).hexdigest()[:16]
        name = 'repro_' + key
        self.jobs[name] = dict(name=name, **job)
        self.reasons.setdefault(name, []).append(reason)
        return name

    def write(self, output, name):
        write_json(output / (name + '.json'), dict(jobs=list(self.jobs.values())))
        write_json(output / (name + '-map.json'), self.reasons)


def diagnostics(stderr, jobs, maps, actual):
    items, counts, attention = [], collections.Counter(), []
    by_name = {j['name']: j for j in jobs}
    by_seed = collections.defaultdict(list)
    for job in jobs:
        for i in range(job['games']):
            by_seed[job['seed'] + i].append((job, job.get('game_index', 0) + i))
    if not stderr.is_file():
        return dict(available=False, error='missing stderr: ' + str(stderr), items=[], attention=[])
    for line_no, line in enumerate(stderr.read_text(errors='replace').splitlines(), 1):
        if '[fd-diverge]' not in line and '[nonconv]' not in line:
            continue
        kind = 'fd-diverge' if '[fd-diverge]' in line else 'nonconv'
        fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
        item = dict(kind=kind, line=line_no, raw=line, leaf_est=fields.get('leaf_est'), fields=fields)
        candidates = []
        if line.count('[fd-diverge]') + line.count('[nonconv]') != 1:
            item.update(classification='interleaved_diagnostic_requires_isolated_repro', attribution='unparsed')
            repro_names = []
            for seed_text in set(re.findall(r'\bseed=(\d+)', line)):
                for job, gi in by_seed.get(int(seed_text), []):
                    repro_names.append(actual.add(dict(job=job, seed=int(seed_text), gi=gi),
                                                   dict(kind=kind, source_line=line_no,
                                                        diagnostic='candidate_from_interleaved_line', source_job=job['name']), actual_world=True))
            item['ordinary_control_repros'] = repro_names
            attention.append(item)
            items.append(item)
            counts[item['classification']] += 1
            continue
        try:
            seed = int(fields['seed'])
        except (KeyError, ValueError):
            item.update(classification='unparsed_diagnostic_requires_review', attribution='unparsed')
            attention.append(item)
            items.append(item)
            counts[item['classification']] += 1
            continue
        if kind == 'fd-diverge' and fields.get('job') in by_name:
            job = by_name[fields['job']]
            try:
                local = int(fields['gi'])
                if not 0 <= local < job['games'] or job['seed'] + local != seed:
                    raise ValueError('seed/local-index mismatch')
                candidates = [(job, job.get('game_index', 0) + local)]
            except (KeyError, ValueError):
                item['index_attribution_error'] = True
        else:
            candidates = by_seed.get(seed, [])
        item['candidate_jobs'] = [dict(job=j['name'], family=maps[j['name']]['family'], gi=gi,
                                      blind=j.get('blind_salt', -1) >= 0,
                                      deck=j['deck']) for j, gi in candidates]
        item['attribution'] = 'exact' if len(candidates) == 1 else 'ambiguous_same_seed' if candidates else 'unattributed'
        blind_states = {j.get('blind_salt', -1) >= 0 for j, _ in candidates}
        item['blind_status'] = 'blind' if blind_states == {True} else 'ordinary' if blind_states == {False} else 'mixed_or_unknown'
        families = sorted({maps[j['name']]['family'] for j, _ in candidates})
        item['families'] = families
        definite_leaf = kind == 'fd-diverge' and fields.get('leaf_est') is not None and fields.get('leaf_est') == fields.get('predicted_win')
        true_simulated = kind == 'fd-diverge' and fields.get('leaf_est') == 'none'
        cyclone = candidates and all('Unpredictable Cyclone' in j['deck'] for j, _ in candidates)
        if item['blind_status'] == 'blind':
            classification = 'blind_forecast_requires_ordinary_world_control'
        elif definite_leaf:
            classification = 'leaf_estimate_warning'
        elif item['blind_status'] == 'ordinary' and cyclone and true_simulated:
            classification = 'BLOCKING_ordinary_cyclone_simulated_fd_divergence'
        elif kind == 'nonconv':
            classification = 'ordinary_nonconv_requires_review' if item['blind_status'] == 'ordinary' else 'ambiguous_nonconv_requires_isolated_control'
        else:
            classification = 'fd_diagnostic_requires_review'
        item['classification'] = classification
        counts[classification] += 1
        if classification not in ('blind_forecast_requires_ordinary_world_control', 'leaf_estimate_warning'):
            attention.append(item)
        repro_names = []
        if not (definite_leaf and item['blind_status'] == 'ordinary'):
            for job, gi in candidates:
                record = dict(job=job, seed=seed, gi=gi)
                repro_names.append(actual.add(record, dict(kind=kind, source_line=line_no,
                                                           diagnostic=classification, source_job=job['name']), actual_world=True))
        item['ordinary_control_repros'] = repro_names
        items.append(item)
    inventory = collections.Counter()
    for item in items:
        for family in (item.get('families') or ['unattributed']):
            inventory[(family, item.get('blind_status', 'unknown'), item['kind'], item['classification'], item['attribution'])] += 1
    return dict(available=True, counts=dict(counts),
                inventory=[dict(family=k[0], blind_status=k[1], kind=k[2], classification=k[3], attribution=k[4], count=v) for k, v in sorted(inventory.items())],
                items=items, attention=attention,
                attribution_note='Nonconv has seed only, so repeated-arm seeds remain candidate sets. FD gi is local to its batch chunk, not global. An ambiguous diagnostic appears in each possible family inventory; do not sum those as distinct events. Blind forecasts are not declared executor defects without ordinary-world reproduction.')


def cast_comparisons(family, names):
    comparisons = []
    for base in sorted(n for n in names if n.startswith('base_')):
        suffix = base[len('base'):]
        arms = [n for n in names if n != base and n.endswith(suffix)]
        comparisons += [(family, base, arm) for arm in sorted(arms)]
        cyc = 'CO_CYC1' + suffix
        comparisons += [(family, cyc, arm) for arm in sorted(arms)
                        if arm.startswith(('CO_CYC1_DRAW_', 'CO_CYC1_MIDDLE_', 'CO_CYC1_REV_CYCLERS_'))]
    return comparisons


def quality(a):
    inputs, out = a.inputs, a.output
    jobs = read_json(inputs / 'primary-quality.json')['jobs']
    plan = read_json(inputs / 'primary-quality-plan.json')
    maps = plan['job_map']
    loaded = load_cells(jobs, a.wins, maps)
    data, expected = loaded['data'], loaded['expected']
    result = dict(complete=not loaded['problems'], problems=loaded['problems'],
                  metric='new-old loss-penalized win round; unwon=max_turns+1; negative is faster',
                  source_tree=plan['src_tree'], binary_sha256=plan.get('binary_sha256'),
                  cells={}, comparisons={}, prunes='Use reconstruct_prunes.py report; not duplicated here.')
    family_cells = collections.defaultdict(set)
    for name, m in maps.items():
        family_cells[m['family']].add(m['logical_cell'])
    recovery = Repros()
    budget = Repros()
    ordinary = Repros()
    for family, names in family_cells.items():
        if family == 'phasec':
            continue
        result['cells'][family] = {name: aggregate(data[name], expected[name]) for name in sorted(names)}
    comparisons = []
    for family in ('cast_q64_raw', 'cast_q64_blind'):
        comparisons += cast_comparisons(family, family_cells[family])
    for family in ('payment_split', 'nested_exhaustion'):
        comparisons += [(family, old, 'on' + old[len('off'):]) for old in sorted(family_cells[family]) if old.startswith('off_')]
    for old in sorted(n for n in family_cells['qc3_leaf_fidelity'] if n.startswith('qc3_leaf1_')):
        suffix = old[len('qc3_leaf1'):]
        comparisons += [('qc3_leaf_fidelity', old, arm + suffix) for arm in ('qc3_first1_rest0', 'qc3_leaf0')]
    for family, old_name, new_name in comparisons:
        key = family + ':' + new_name + '-vs-' + old_name
        if new_name not in expected or expected[old_name] != expected[new_name]:
            result['problems'].append(dict(comparison=key, error='missing/mismatched expected cell'))
            continue
        old, new = data[old_name], data[new_name]
        if not a.allow_partial and (len(old) != expected[old_name] or len(new) != expected[new_name]):
            result['comparisons'][key] = dict(complete=False, error='comparison withheld until both cells complete')
            continue
        pair = paired(old, new, expected[old_name])
        for row in pair['slower_games']:
            identity = (row['seed'], row['gi'])
            for role, records in (('old', old), ('new', new)):
                record = records[identity]
                reason = dict(comparison=key, role=role, seed=row['seed'], gi=row['gi'], finite_delta=row['delta'])
                row[role + '_d8b0'] = recovery.add(record, reason, depth=8)
                for multiplier in (4, 16):
                    budget.add(record, dict(reason, multiplier=multiplier), multiplier=multiplier)
                trace = a.traces / (record['job_name'] + f"_gi{row['gi']}.json")
                row[role + '_trace'] = str(trace)
                if not trace.is_file():
                    result['problems'].append(dict(job=record['job_name'], error='missing slower-game trace: ' + str(trace)))
        result['comparisons'][key] = pair
    qc1_helper_spec = importlib.util.spec_from_file_location('cyclone_manifest_helper', HERE / 'make_validation_manifests.py')
    qc1_helper = importlib.util.module_from_spec(qc1_helper_spec)
    qc1_helper_spec.loader.exec_module(qc1_helper)
    try:
        selected = read_json(inputs / 'qc1-selection.json')['selected']
        result['qc1'] = qc1_helper.qc1_report(selected, a.wins, a.traces)
        by_name = {j['name']: j for j in jobs}
        for row in result['qc1']['games']:
            for arm in ('A', 'B'):
                name = f"qc1_{arm}_s{row['base_seed']}_gi{row['game_index']}_c{row['creature_cyclers']}"
                point = data[name][(row['seed'], row['game_index'])]
                row[arm + '_units'] = point['units']
                row[arm + '_digest'] = point['digest']
            if row['delta'] <= 0:
                continue
            for arm in ('A', 'B'):
                name = f"qc1_{arm}_s{row['base_seed']}_gi{row['game_index']}_c{row['creature_cyclers']}"
                record = dict(job=by_name[name], seed=row['seed'], gi=row['game_index'])
                recovery.add(record, dict(comparison='qc1', arm=arm, seed=row['seed'], gi=row['game_index']), depth=8)
        for label, group in result['qc1']['groups'].items():
            rows = [r for r in result['qc1']['games'] if label == 'all'
                    or label == r['cycler_group'] or label == r['cause']]
            old_u, new_u = sum(r['A_units'] for r in rows), sum(r['B_units'] for r in rows)
            group.update(A_total_units=old_u, B_total_units=new_u, units_ratio=new_u / old_u if old_u else None,
                         digest_changed=sum(r['A_digest'] != r['B_digest'] for r in rows))
    except (OSError, KeyError, ValueError) as error:
        result['qc1'] = dict(complete=False, error=str(error))
        result['problems'].append(dict(family='qc1', error=str(error)))
    # Explicitly owed Q64 oracles even if the final source happens to stop losing
    # these two games. Original raw gi110 and blind gi94 pinpointed the tier.
    for family, seed, gi in (('cast_q64_raw', 131110, 110), ('cast_q64_blind', 135094, 94)):
        for name in sorted(family_cells[family]):
            if '_d3_' not in name:
                continue
            record = data.get(name, {}).get((seed, gi))
            if record:
                recovery.add(record, dict(comparison='Q64_required_oracle', cell=name, seed=seed, gi=gi), depth=8)
    result['diagnostics'] = diagnostics(a.stderr, jobs, maps, ordinary)
    if not result['diagnostics']['available']:
        result['problems'].append(dict(family='diagnostics', error=result['diagnostics']['error']))
    result['complete'] = not result['problems']
    result['fidelity_requires_review'] = bool(result['diagnostics'].get('attention'))
    result['repros'] = dict(d8b0=len(recovery.jobs), larger_budget=len(budget.jobs), ordinary_world=len(ordinary.jobs), launched=False)
    logical = {}
    for family, names in family_cells.items():
        if family == 'phasec':
            continue
        logical[family] = {name: [dict(seed=r['seed'], gi=r['gi'], win=r['win'], digest=r['digest'],
                                     units=r['units'], source_job=r['job_name'])
                                 for _, r in sorted(data[name].items())] for name in sorted(names)}
    write_json(out / 'logical-games.json', logical)
    recovery.write(out, 'slower-recovery-d8b0')
    budget.write(out, 'slower-budget-recovery')
    ordinary.write(out, 'diagnostic-ordinary-world')
    repro_plan = dict(source_tree=plan['src_tree'], binary_sha256=plan.get('binary_sha256'), launched=False, stages=[])
    for name, count in (('slower-recovery-d8b0', len(recovery.jobs)), ('slower-budget-recovery', len(budget.jobs)),
                        ('diagnostic-ordinary-world', len(ordinary.jobs))):
        run = out / 'repro-runs' / name
        repro_plan['stages'].append(dict(name=name, jobs=count, cwd=plan['cwd'],
            argv=[plan['binary'], '--batch', str(out / (name + '.json')), '--threads', '12',
                  '--game-log-dir', str(run / 'wins'), '--game-trace-dir', str(run / 'traces')],
            env={'MTG_DUMP_UNITS': '1', 'MTG_FD_ORACLE': '1', 'MTG_FLAG_NONCONV': '1', 'MTG_SLOW_GAME_MS': '1'},
            unset_before_run=plan.get('unset_before_run', []), stdout=str(run / 'batch.log'), stderr=str(run / 'batch.err'),
            note='Manifest only; skip empty stages. Read existing primary traces before spending a reproduction. No timing/timeout caps were added.'))
    write_json(out / 'repro-runtime-plan.json', repro_plan)
    write_json(out / 'quality-report.json', result)
    lines = ['# Cyclone primary quality results', '', f"Complete: {result['complete']}; fidelity requires review: {result['fidelity_requires_review']}.", '',
             'Delta is new minus control, with an unwon game scored9. Negative is faster.', '',
             '| Comparison | n | Delta | SE | Faster | Slower | Units ratio |', '|---|---:|---:|---:|---:|---:|---:|']
    for name, r in result['comparisons'].items():
        if 'n' not in r:
            lines.append(f"| {name} | incomplete | | | | | |")
        else:
            def fmt(v): return 'n/a' if v is None else f'{v:.5f}'
            lines.append(f"| {name} | {r['n']}/{r['expected']} | {fmt(r['delta'])} | {fmt(r['se'])} | {r['faster']} | {r['slower']} | {fmt(r['units_ratio'])} |")
    lines += ['', 'QC1 exact-hand/initial-keep handshake and strata are in quality-report.json. Pruning comparisons use their dedicated reporter.', '',
              'FD/nonconv records retain leaf_est and candidate attribution. Blind future resampling is reported separately from ordinary-world simulated divergence.', '',
              'Reproduction manifests were emitted only; none were run. Existing slower-game trace paths accompany every pair.']
    (out / 'quality-report.md').write_text('\n'.join(lines) + '\n')
    print(json.dumps(dict(report=str(out / 'quality-report.json'), complete=result['complete'],
                          problems=len(result['problems']), fidelity_requires_review=result['fidelity_requires_review'], repros=result['repros']), indent=2))
    return 2 if not result['complete'] else 1 if result['fidelity_requires_review'] else 0


def cache_pressure(path):
    """Read process-exit admission counts; FSL accounting is a separate caveat."""
    path = Path(path)
    result = dict(stderr=str(path), plan_pool=None, fsl_refused_stores=None,
                  fsl_warnings=[], determinism_records=[], problems=[])
    if not path.is_file():
        result['problems'].append('missing cache-pressure stderr: ' + str(path))
        return result
    text = path.read_text(errors='replace')
    summaries = re.findall(r'\[plan-pool\] budget=(\d+) KB hiwater=(\d+) KB residual=(\d+) KB refused=(\d+)', text)
    if len(summaries) != 1:
        result['problems'].append(f'expected one process-exit plan-pool summary; found {len(summaries)}')
        return result
    result['plan_pool'] = dict(zip(('budget_kb', 'hiwater_kb', 'residual_kb', 'refused_stores'), map(int, summaries[0])))
    result['determinism_records'] = [line for line in text.splitlines() if '[determinism]' in line]
    result['fsl_warnings'] = [line for line in result['determinism_records'] if 'WARNING:' in line and 'FSL' in line]
    warnings = re.findall(r'\[determinism\] WARNING: the shared memory pools refused stores \(plan cache (\d+), FSL line cache (\d+)\)', text)
    neutral = re.findall(r'\[determinism\] plan cache refused (\d+) stores[^\n]*FSL did not bind\.', text)
    if len(warnings) + len(neutral) > 1:
        result['problems'].append('multiple or conflicting process-exit determinism summaries')
    elif warnings:
        plan, fsl = map(int, warnings[0])
        result['fsl_refused_stores'] = fsl
        if plan != result['plan_pool']['refused_stores']:
            result['problems'].append('plan-pool and determinism refusal counts disagree')
    elif neutral:
        result['fsl_refused_stores'] = 0
        if int(neutral[0]) != result['plan_pool']['refused_stores']:
            result['problems'].append('plan-pool and neutral determinism refusal counts disagree')
    elif result['plan_pool']['refused_stores'] > 0 or result['determinism_records']:
        result['problems'].append('missing/unrecognized determinism summary for refused stores')
    else:
        # DeterminismReport is silent only when both process-global refusal
        # counters are zero. The exit plan-pool summary distinguishes this from
        # a run without the requested statistics or an absent stderr file.
        result['fsl_refused_stores'] = 0
    result['fsl_bound'] = None if result['fsl_refused_stores'] is None else result['fsl_refused_stores'] > 0
    result['fsl_caveat'] = ('FSL still discounts hits; binding shared FSL admission retains the existing '
                            'thread/interleaving limitation. Observed equality is not a general FSL invariance proof.')
    return result


def memory_diagnostics(path, plan, jobs=None, maps=None):
    """Strict ordinary-world fidelity gate, independent of outcome equality.

    Standalone memory processes contain no blind arms: every FD/nonconv record,
    including a leaf-estimate warning, requires review. For the mixed primary
    pool, use attribution to select memory records; blind forecasts in other
    families remain the quality reporter's separate ordinary-control obligation.
    """
    path = Path(path)
    result = dict(stderr=str(path), items=[], fatal=[], problems=[])
    for key in ('MTG_FD_ORACLE', 'MTG_FLAG_NONCONV'):
        if plan.get('env', {}).get(key) != '1':
            result['problems'].append('fidelity diagnostic was not enabled in recorded plan: ' + key)
    if not path.is_file():
        result['problems'].append('missing fidelity stderr: ' + str(path))
        result['clean'] = False
        return result
    if jobs is None:
        for number, line in enumerate(path.read_text(errors='replace').splitlines(), 1):
            if '[fd-diverge]' in line or '[nonconv]' in line:
                fields = dict(re.findall(r'(\w+)=([^\s]+)', line))
                result['items'].append(dict(line=number, raw=line, fields=fields,
                    kind='fd-diverge' if '[fd-diverge]' in line else 'nonconv',
                    classification='BLOCKING_memory_ordinary_fidelity_diagnostic'))
        result['fatal'] = list(result['items'])
    else:
        inventory = diagnostics(path, jobs, maps, Repros())
        result['items'] = inventory['items']
        for item in inventory['items']:
            families = item.get('families', [])
            if not families or set(families) & {'memory_invariance', 'memory_cyclone_shapes'}:
                result['fatal'].append(item)
        result['scope'] = 'Mixed primary pool: all memory-attributed or unattributed diagnostics block; other families remain in quality inventory.'
    result['clean'] = not result['problems'] and not result['fatal']
    return result


def memory_preflight(a):
    """Compare the planned t1/t6 overlap without reading any primary results."""
    names = ('memory_t1_normal', 'memory_t6_both_reference_sets')
    schedule = read_json(a.inputs / 'final-runtime-plan.json')
    variants = {v['name']: v for v in schedule['memory_comparisons']}
    overrides = dict(x.split('=', 1) for x in a.run)
    stderr_overrides = dict(x.split('=', 1) for x in a.stderr)
    result = dict(scope='Interim normal t1 versus t6 shared games only; no t12/tiny or full memory proof.',
                  runs={}, cache_pressure={}, fidelity={}, cells={}, six_only_cells={}, differences=[], problems=[])
    missing = sorted(set(names) - set(variants))
    if missing:
        result.update(complete=False, observed_exact=False, preflight_pass=False)
        result['problems'].append(dict(error='required preflight runtime plans absent', plans=missing))
        write_json(a.output / 'memory-preflight-report.json', result)
        print(json.dumps(dict(report=str(a.output / 'memory-preflight-report.json'), complete=False, problems=result['problems']), indent=2))
        return 2
    loaded = {}
    for name in names:
        variant = variants[name]
        manifest = Path(variant['argv'][variant['argv'].index('--batch') + 1])
        jobs = read_json(manifest)['jobs']
        wins = Path(overrides.get(name, Path(variant['stdout']).parent / 'wins'))
        loaded[name] = load_cells(jobs, wins)
        result['runs'][name] = dict(manifest=str(manifest), wins=str(wins),
                                   expected_games=sum(loaded[name]['expected'].values()))
        result['problems'] += [dict(variant=name, **p) for p in loaded[name]['problems']]
        pressure = cache_pressure(stderr_overrides.get(name, variant['stderr']))
        result['cache_pressure'][name] = pressure
        result['problems'] += [dict(variant=name, error=p) for p in pressure['problems']]
        fidelity = memory_diagnostics(stderr_overrides.get(name, variant['stderr']), variant)
        result['fidelity'][name] = fidelity
        result['problems'] += [dict(variant=name, error=p) for p in fidelity['problems']]
    one, six = (loaded[name] for name in names)
    shared = set(one['data']) & set(six['data'])
    result['shared_expected'] = sum(one['expected'].values())
    result['shared_compared'] = 0
    for cell in sorted(one['data']):
        aa, bb = one['data'][cell], six['data'].get(cell, {})
        expected = one['expected'][cell]
        if cell not in shared or six['expected'].get(cell) != expected or len(aa) != expected or len(bb) != expected:
            result['problems'].append(dict(cell=cell, error='shared cell missing/incomplete or planned counts differ'))
        if set(aa) != set(bb):
            result['problems'].append(dict(cell=cell, error='different physical game identities',
                only_t1=[list(k) for k in sorted(set(aa) - set(bb))], only_t6=[list(k) for k in sorted(set(bb) - set(aa))]))
        summary = dict(expected=expected, compared=len(set(aa) & set(bb)),
                       win_differences=0, digest_differences=0, unit_differences=0)
        result['shared_compared'] += summary['compared']
        for key in sorted(set(aa) & set(bb)):
            changed = [field for field in ('win', 'digest', 'units') if aa[key][field] != bb[key][field]]
            for field in changed:
                summary[{'win': 'win_differences', 'digest': 'digest_differences', 'units': 'unit_differences'}[field]] += 1
            if changed:
                result['differences'].append(dict(cell=cell, seed=key[0], gi=key[1], changed=changed,
                    t1={k: aa[key][k] for k in ('win', 'digest', 'units')},
                    t6={k: bb[key][k] for k in ('win', 'digest', 'units')}))
        result['cells'][cell] = summary
    for cell in sorted(set(six['data']) - set(one['data'])):
        result['six_only_cells'][cell] = aggregate(six['data'][cell], six['expected'][cell])
        if len(six['data'][cell]) != six['expected'][cell]:
            result['problems'].append(dict(cell=cell, error='six-worker-only shapes incomplete'))
    result['six_only_expected'] = sum(six['expected'][cell] for cell in result['six_only_cells'])
    unknown = (set(overrides) | set(stderr_overrides)) - set(names)
    if unknown:
        result['problems'].append(dict(error='unknown run/log overrides: ' + str(sorted(unknown))))
    result['complete'] = not result['problems']
    result['observed_exact'] = result['complete'] and not result['differences'] and result['shared_compared'] == result['shared_expected']
    result['fsl_review_required'] = any((p['fsl_refused_stores'] or 0) > 0 for p in result['cache_pressure'].values())
    result['determinism_review_required'] = result['fsl_review_required'] or any(p['fsl_warnings'] for p in result['cache_pressure'].values())
    result['fidelity_clean'] = all(d['clean'] for d in result['fidelity'].values())
    result['preflight_pass'] = result['observed_exact'] and not result['determinism_review_required'] and result['fidelity_clean']
    result['warning_scope'] = 'Refusal counts are process-wide: t6 includes its extra shapes and cannot attribute FSL pressure solely to the shared168 games.'
    write_json(a.output / 'memory-preflight-report.json', result)
    print(json.dumps(dict(report=str(a.output / 'memory-preflight-report.json'), complete=result['complete'],
        observed_exact=result['observed_exact'], shared_compared=result['shared_compared'], shared_expected=result['shared_expected'],
        six_only_expected=result['six_only_expected'], fsl_review_required=result['fsl_review_required'],
        preflight_pass=result['preflight_pass'], fidelity_clean=result['fidelity_clean'],
        differences=len(result['differences']), problems=len(result['problems'])), indent=2))
    return 2 if not result['complete'] else 1 if not result['preflight_pass'] else 0


def shape_comparisons(cells, normal_runs, expected, primary_threads):
    comparisons, failures = {}, []
    pairs = dict.fromkeys(tuple(sorted(pair)) for pair in
                          ((6, 12), (6, primary_threads), (12, primary_threads)) if pair[0] != pair[1])
    for left, right in pairs:
        key = f'normal{left}-vs-normal{right}'
        row = dict(expected=sum(expected[c] for c in cells), compared=0, differences=[])
        aa, bb = normal_runs.get(left), normal_runs.get(right)
        complete = aa is not None and bb is not None
        for cell in sorted(cells):
            a = aa['data'].get(cell, {}) if aa else {}
            b = bb['data'].get(cell, {}) if bb else {}
            complete = complete and len(a) == len(b) == expected[cell] and set(a) == set(b)
            for identity in sorted(set(a) & set(b)):
                row['compared'] += 1
                changed = [k for k in ('win', 'digest', 'units') if a[identity][k] != b[identity][k]]
                if changed:
                    row['differences'].append(dict(cell=cell, seed=identity[0], gi=identity[1], changed=changed))
        row['exact'] = complete and row['compared'] == row['expected'] and not row['differences']
        comparisons[key] = row
        if not row['exact']:
            failures.append(dict(comparison=key, error='required shape comparison missing, incomplete or different'))
    return comparisons, failures


def memory(a):
    inputs = a.inputs
    plan = read_json(inputs / 'primary-quality-plan.json')
    primary_threads = protocol.primary_threads(None, plan)
    maps = plan['job_map']
    primary_jobs = read_json(inputs / 'primary-quality.json')['jobs']
    jobs = [j for j in primary_jobs
            if maps[j['name']]['family'] in ('memory_invariance', 'memory_cyclone_shapes')]
    baseline = load_cells(jobs, a.primary_wins, maps)
    schedule = read_json(inputs / 'final-runtime-plan.json')
    overrides = dict(x.split('=', 1) for x in a.run)
    stderr_overrides = dict(x.split('=', 1) for x in a.stderr)
    primary_pressure = cache_pressure(a.primary_stderr)
    result = dict(baseline=f'primary normal{primary_threads}, chunked', primary_threads=primary_threads,
                  comparisons={}, problems=baseline['problems'], differences=[],
                  cache_pressure={'primary': primary_pressure}, pressure_failures=[], reference_failures=[], fidelity={})
    normal_runs = {primary_threads: baseline}
    result['problems'] += [dict(variant='primary', error=p) for p in primary_pressure['problems']]
    result['fidelity']['primary'] = memory_diagnostics(a.primary_stderr, plan, primary_jobs, maps)
    result['problems'] += [dict(variant='primary', error=p) for p in result['fidelity']['primary']['problems']]
    for variant in schedule['memory_comparisons']:
        name = variant['name']
        manifest = Path(variant['argv'][variant['argv'].index('--batch') + 1])
        expected_jobs = read_json(manifest)['jobs']
        wins = Path(overrides[name]) if name in overrides else Path(variant['stdout']).parent / 'wins'
        trial = load_cells(expected_jobs, wins)
        result['problems'] += [dict(variant=name, **p) for p in trial['problems']]
        pressure = cache_pressure(stderr_overrides.get(name, variant['stderr']))
        result['cache_pressure'][name] = pressure
        result['problems'] += [dict(variant=name, error=p) for p in pressure['problems']]
        fidelity = memory_diagnostics(stderr_overrides.get(name, variant['stderr']), variant)
        result['fidelity'][name] = fidelity
        result['problems'] += [dict(variant=name, error=p) for p in fidelity['problems']]
        if name == 'memory_t12_tiny':
            pp = pressure['plan_pool']
            proven = pp is not None and pp['budget_kb'] == 20480 and pp['refused_stores'] > 0 and not pressure['problems']
            result['tiny12_shared_plan_pressure_proven'] = proven
            if not proven:
                result['pressure_failures'].append(dict(variant=name,
                    error='Tiny12 proof requires an exit summary with a shared20480KB plan budget and positive refused stores',
                    observed=pp))
        summary = dict(expected=sum(trial['expected'].values()), compared=0, win_differences=0, digest_differences=0, unit_differences=0)
        for cell, rows in trial['data'].items():
            control = baseline['data'].get(cell, {})
            if len(rows) != trial['expected'][cell] or len(control) != baseline['expected'].get(cell):
                result['problems'].append(dict(variant=name, cell=cell, error='incomplete cell; equality not claimed'))
            if set(rows) != set(control):
                result['problems'].append(dict(variant=name, cell=cell, error='different physical game identities',
                                               only_trial=[list(k) for k in sorted(set(rows) - set(control))],
                                               only_primary=[list(k) for k in sorted(set(control) - set(rows))]))
            for key in sorted(set(rows) & set(control)):
                aa, bb = control[key], rows[key]
                summary['compared'] += 1
                changed = []
                for field, counter in (('win', 'win_differences'), ('digest', 'digest_differences'), ('units', 'unit_differences')):
                    if aa[field] != bb[field]:
                        summary[counter] += 1
                        changed.append(field)
                if changed:
                    result['differences'].append(dict(variant=name, cell=cell, seed=key[0], gi=key[1], changed=changed,
                                                      primary={k: aa[k] for k in ('win', 'digest', 'units')},
                                                      trial={k: bb[k] for k in ('win', 'digest', 'units')}))
        summary['exact'] = summary['compared'] == summary['expected'] and not any(summary[k] for k in ('win_differences', 'digest_differences', 'unit_differences'))
        result['comparisons'][name] = summary
        if name in ('memory_t6_both_reference_sets', 'memory_t12_both_reference_sets'):
            workers = protocol.primary_threads(None, variant)
            expected_workers = 6 if name.startswith('memory_t6_') else 12
            if workers != expected_workers:
                result['reference_failures'].append(dict(variant=name, error='normal reference worker count disagrees with its label'))
            else:
                normal_runs[workers] = trial
            if expected_workers == 12:
                proven = (workers == 12 and trial['expected'] == baseline['expected']
                          and summary['exact'] and fidelity['clean'])
                result['normal12_reference_proven'] = proven
                if not proven:
                    result['reference_failures'].append(dict(variant=name,
                        error='normal12 proof requires both complete reference sets matching primary and clean fidelity'))
    unknown = (set(overrides) | set(stderr_overrides)) - set(result['comparisons'])
    if unknown:
        result['problems'].append(dict(error='unknown --run overrides: ' + str(sorted(unknown))))
    result['complete'] = not result['problems']
    result['exact'] = result['complete'] and not result['differences']
    if 'tiny12_shared_plan_pressure_proven' not in result:
        result['tiny12_shared_plan_pressure_proven'] = False
        result['pressure_failures'].append(dict(error='required memory_t12_tiny comparison absent from runtime plan'))
    if 'normal12_reference_proven' not in result:
        result['normal12_reference_proven'] = False
        result['reference_failures'].append(dict(error='required independent memory_t12_both_reference_sets comparison absent'))
    shape_cells = {maps[j['name']]['logical_cell'] for j in jobs
                   if maps[j['name']]['family'] == 'memory_cyclone_shapes'}
    if shape_cells:
        result['shape_comparisons'], shape_failures = shape_comparisons(shape_cells, normal_runs, baseline['expected'], primary_threads)
        result['reference_failures'] += shape_failures
    else:
        result['shape_comparisons'] = {}
    result['fidelity_clean'] = all(d['clean'] for d in result['fidelity'].values())
    result['proof_pass'] = (result['exact'] and result['tiny12_shared_plan_pressure_proven']
                            and result['normal12_reference_proven'] and not result['reference_failures']
                            and result['fidelity_clean'])
    write_json(a.output / 'memory-report.json', result)
    print(json.dumps(dict(report=str(a.output / 'memory-report.json'), complete=result['complete'], exact=result['exact'],
                          proof_pass=result['proof_pass'], fidelity_clean=result['fidelity_clean'],
                          tiny12_shared_plan_pressure_proven=result['tiny12_shared_plan_pressure_proven'],
                          normal12_reference_proven=result['normal12_reference_proven'],
                          problems=len(result['problems']), differences=len(result['differences']), comparisons=result['comparisons']), indent=2))
    return 2 if not result['complete'] else 1 if not result['proof_pass'] else 0


def parse_args(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest='mode', required=True)
    q = sub.add_parser('quality')
    q.add_argument('--inputs', type=Path, default=DEFAULT)
    q.add_argument('--wins', type=Path)
    q.add_argument('--traces', type=Path)
    q.add_argument('--stderr', type=Path)
    q.add_argument('--output', type=Path)
    q.add_argument('--allow-partial', action='store_true', help='labelled partial/intersection metrics; still nonzero for missing data')
    m = sub.add_parser('memory')
    m.add_argument('--inputs', type=Path, default=DEFAULT)
    m.add_argument('--primary-wins', type=Path)
    m.add_argument('--primary-stderr', type=Path)
    m.add_argument('--run', action='append', default=[], metavar='PLAN_NAME=WINS_DIR')
    m.add_argument('--stderr', action='append', default=[], metavar='PLAN_NAME=STDERR_FILE')
    m.add_argument('--output', type=Path)
    p = sub.add_parser('memory-preflight', help='normal t1/t6 overlap only; requires review if FSL binds')
    p.add_argument('--inputs', type=Path, default=DEFAULT)
    p.add_argument('--run', action='append', default=[], metavar='PLAN_NAME=WINS_DIR')
    p.add_argument('--stderr', action='append', default=[], metavar='PLAN_NAME=STDERR_FILE')
    p.add_argument('--output', type=Path)
    a = ap.parse_args(argv)
    primary = a.inputs / 'runs/primary-quality'
    if a.output is None:
        a.output = a.inputs / 'reports'
    if a.mode == 'quality':
        for key, filename in (('wins', 'wins'), ('traces', 'traces'), ('stderr', 'batch.err')):
            if getattr(a, key) is None:
                setattr(a, key, primary / filename)
    elif a.mode == 'memory':
        if a.primary_wins is None:
            a.primary_wins = primary / 'wins'
        if a.primary_stderr is None:
            a.primary_stderr = primary / 'batch.err'
    return a


def main():
    a = parse_args()
    return {'quality': quality, 'memory': memory, 'memory-preflight': memory_preflight}[a.mode](a)


if __name__ == '__main__':
    raise SystemExit(main())
