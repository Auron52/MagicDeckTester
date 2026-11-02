"""Pure preparation policy shared by helpers; no filesystem or engine actions."""
import copy


def primary_threads(explicit, *plans):
    recorded = []
    for plan in plans:
        if not plan:
            continue
        for key in ('primary_threads', 'threads'):
            if key in plan:
                recorded.append(int(plan[key]))
        argv = plan.get('argv', [])
        if '--threads' in argv:
            recorded.append(int(argv[argv.index('--threads') + 1]))
    if len(set(recorded)) > 1:
        raise ValueError('Stored primary thread counts disagree; use fresh consistent inputs')
    chosen = explicit if explicit is not None else recorded[0] if recorded else None
    if chosen is None or chosen < 1:
        raise ValueError('Specify positive --threads or regenerate inputs with a stored primary thread count')
    if recorded and chosen != recorded[0]:
        raise ValueError('Explicit --threads differs from stored primary plan; use fresh inputs to change the protocol')
    return chosen


def frontload_job(family, job):
    result = dict(job)
    known_cast_fd = (family == 'cast_q64_raw'
                     and job['seed'] <= 131117 < job['seed'] + job['games']
                     and job.get('game_index', 0) + 131117 - job['seed'] == 117)
    if family == 'qc3_leaf_fidelity' or known_cast_fd:
        result['weight'] = 10000000 - job.get('game_index', 0)
    return result


def memory_comparisons(standalone, combined_manifest):
    def combined(threads):
        plan = copy.deepcopy(standalone[f'memory_t{threads}_normal'])
        plan['name'] = f'memory_t{threads}_both_reference_sets'
        plan['argv'][plan['argv'].index('--batch') + 1] = str(combined_manifest)
        return plan
    return [copy.deepcopy(standalone['memory_t1_normal']), combined(6), combined(12),
            copy.deepcopy(standalone['memory_t12_tiny']), copy.deepcopy(standalone['memory_t6_unbounded'])]
