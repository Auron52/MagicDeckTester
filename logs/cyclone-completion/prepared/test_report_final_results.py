#!/usr/bin/env python3
"""Synthetic parser/report checks only: never starts the engine."""
import argparse
import copy
import contextlib
import importlib.util
import io
import json
import tempfile
import unittest
from unittest import mock
from pathlib import Path

spec = importlib.util.spec_from_file_location('cyclone_report', Path(__file__).with_name('report_final_results.py'))
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)
FIDELITY_ENV = {'MTG_FD_ORACLE': '1', 'MTG_FLAG_NONCONV': '1'}


def sibling_module(name):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


manifests = sibling_module('make_validation_manifests')
launcher = sibling_module('run_prepared_validation')
protocol = sibling_module('prepared_protocol')


def pool(budget=20480, refused=3, fsl=0):
    text = f'[plan-pool] budget={budget} KB hiwater=20470 KB residual=0 KB refused={refused}\n'
    if fsl:
        text += (f'[determinism] WARNING: the shared memory pools refused stores (plan cache {refused}, FSL line cache {fsl}). '
                 'Per-game work units -- and so budgeted play at the start gate\'s edge -- are only guaranteed identical across thread counts and interleavings when no pool binds.\n')
    elif refused:
        text += (f'[determinism] plan cache refused {refused} stores -- shared admission with exact keys, isolated channels '
                 'and hit-equals-miss pricing; FSL did not bind.\n')
    return text


class ReportTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)

    def test_primary_workers_resolve_and_reject_stored_mismatches(self):
        own = {'primary_threads': 24}
        sealed = {'threads': 24, 'argv': ['mtg', '--threads', '24']}
        self.assertEqual(protocol.primary_threads(None, own, sealed), 24)
        self.assertEqual(protocol.primary_threads(24, own, sealed), 24)
        for explicit, plans in ((12, (own, sealed)), (None, ({'threads': 24, 'argv': ['mtg', '--threads', '12']},)),
                                (None, ({},)), (0, ())):
            with self.subTest(explicit=explicit, plans=plans), self.assertRaises(ValueError):
                protocol.primary_threads(explicit, *plans)

    def test_launcher_passes_stored24_to_seal_without_launching(self):
        inputs = self.root/'launch-inputs'; inputs.mkdir()
        report.write_json(inputs/'run-plan.json', dict(primary_threads=24,
            qc1_sampler_provenance={'recorded': {'inputs': {}}}))
        primary = dict(threads=24, argv=['mtg', '--threads', '24'])
        report.write_json(inputs/'primary-quality-plan.json', primary)
        report.write_json(inputs/'final-runtime-plan.json', {'primary_quality': primary})
        argv = ['launcher', 'primary', '--worktree', str(self.root), '--inputs', str(inputs),
                '--prunes', str(self.root/'manifest.json'), '--dry-run']
        with mock.patch('sys.argv', argv), mock.patch.object(launcher.subprocess, 'run') as called, \
                contextlib.redirect_stdout(io.StringIO()):
            launcher.main()
        seal = called.call_args.args[0]
        self.assertEqual(seal[seal.index('--threads') + 1], '24')
        self.assertIn('--seal', seal)
        with mock.patch('sys.argv', argv + ['--threads', '12']), \
                mock.patch.object(launcher.subprocess, 'run') as called, self.assertRaises(ValueError):
            launcher.main()
        called.assert_not_called()

    def test_frontload_preserves_semantics_and_hits_all_six_known_cast_chunks(self):
        arms = ('base', 'CO', 'CO_CYC1', 'CO_CYC1_DRAW', 'CO_CYC1_REV_CYCLERS', 'CO_CYC1_MIDDLE')
        for arm in arms:
            original = dict(name=arm+'_d3_s131000_off110', seed=131110, game_index=110, games=10,
                            weight=99989, depth=3, budget_ms=10, flags={'test': True}, profile='same-profile')
            snapshot = copy.deepcopy(original)
            prioritized = protocol.frontload_job('cast_q64_raw', original)
            self.assertGreater(prioritized['weight'], 100000)
            self.assertEqual({k:v for k,v in prioritized.items() if k != 'weight'},
                             {k:v for k,v in original.items() if k != 'weight'})
            self.assertEqual(original, snapshot)
            self.assertEqual(protocol.frontload_job('cast_q64_blind', original), original)
        qc3 = dict(seed=127010, game_index=10, games=10, weight=99999)
        self.assertGreater(protocol.frontload_job('qc3_leaf_fidelity', qc3)['weight'], 100000)
        shape = dict(seed=107000, game_index=0, games=10, weight=100000)
        self.assertEqual(protocol.frontload_job('memory_cyclone_shapes', shape), shape)
        later = dict(seed=131120, game_index=120, games=10, weight=99988)
        self.assertEqual(protocol.frontload_job('cast_q64_raw', later), later)

    def test_independent_normal12_reuses_both_reference_sets_with12_workers(self):
        def plan(name, workers):
            return dict(name=name, argv=['mtg','--batch','core.json','--threads',str(workers)],
                        env={'unchanged': '1'}, stdout=name+'/batch.log', stderr=name+'/batch.err')
        names = [('memory_t1_normal',1), ('memory_t6_normal',6), ('memory_t12_normal',12),
                 ('memory_t24_normal',24), ('memory_t12_tiny',12), ('memory_t6_unbounded',6)]
        original = {n:plan(n,t) for n,t in names}; snapshot = copy.deepcopy(original)
        comparisons = protocol.memory_comparisons(original, 'both468.json')
        self.assertEqual([p['name'] for p in comparisons], ['memory_t1_normal', 'memory_t6_both_reference_sets',
            'memory_t12_both_reference_sets', 'memory_t12_tiny', 'memory_t6_unbounded'])
        for p, workers in zip(comparisons[1:3], (6,12)):
            self.assertEqual(p['argv'][p['argv'].index('--batch')+1], 'both468.json')
            self.assertEqual(p['argv'][p['argv'].index('--threads')+1], str(workers))
        self.assertEqual(original, snapshot)
        comparisons[2]['env']['unchanged'] = 'changed'
        self.assertEqual(original, snapshot)

    def test_shape_matrix_reports6_12_24_and_detects12_only_difference(self):
        row = {'win': 4, 'digest': 'same', 'units': 100}
        runs = {t:{'data': {'shape': {(107008,8):copy.deepcopy(row)}}} for t in (6,12,24)}
        matrix, failures = report.shape_comparisons({'shape'}, runs, {'shape':1}, 24)
        self.assertEqual(set(matrix), {'normal6-vs-normal12','normal6-vs-normal24','normal12-vs-normal24'})
        self.assertFalse(failures)
        runs[12]['data']['shape'][(107008,8)]['units'] = 101
        matrix, failures = report.shape_comparisons({'shape'}, runs, {'shape':1}, 24)
        self.assertTrue(matrix['normal6-vs-normal24']['exact'])
        self.assertFalse(matrix['normal12-vs-normal24']['exact'])
        self.assertEqual(len(failures), 2)

    def test_inherited_cache_overrides_are_cleared_before_intentional_pins(self):
        hostile = {'MTG_FSL_POOL': '1', 'MTG_FSL_CAP': '1', 'MTG_MEM_BUDGET_MB': '1',
                   'MTG_PLAN_CACHE_EXACT_KEY': '0', 'MTG_BP_DERIVE_ISOLATE': '0',
                   'MTG_CACHE_HIT_CHARGE': '0', 'MTG_PLAN_CACHE_KB': '1', 'PATH': '/test/bin'}
        unset = manifests.PROCESS_CACHE_OVERRIDES | {'MTG_PLAN_CACHE_KB'}
        clean = launcher.launch_environment(hostile, unset, FIDELITY_ENV)
        self.assertFalse(set(hostile).intersection(clean) - {'PATH'})
        self.assertEqual(clean['PATH'], '/test/bin')
        for capacity in ('20480', '0'):
            with self.subTest(capacity=capacity):
                pins = dict(FIDELITY_ENV, MTG_PLAN_CACHE_KB=capacity,
                            MTG_PLAN_CACHE_EXACT_KEY='1', MTG_BP_DERIVE_ISOLATE='1', MTG_CACHE_HIT_CHARGE='1')
                configured = launcher.launch_environment(hostile, unset, pins)
                for key, value in pins.items():
                    self.assertEqual(configured[key], value)
                self.assertNotIn('MTG_FSL_POOL', configured)
                self.assertNotIn('MTG_MEM_BUDGET_MB', configured)
        self.assertEqual(hostile['MTG_CACHE_HIT_CHARGE'], '0')

    def test_cast_draw_compares_against_base_and_cyc1_in_both_cells(self):
        arms=('base','CO','CO_CYC1','CO_CYC1_DRAW','CO_CYC1_REV_CYCLERS','CO_CYC1_MIDDLE')
        for family in ('cast_q64_raw','cast_q64_blind'):
            suffixes=('_d3_s131000','_d5_s132000') if family.endswith('raw') else ('_d3_s135000','_d5_s136000')
            names={arm+suffix for arm in arms for suffix in suffixes}
            pairs=report.cast_comparisons(family,names)
            self.assertEqual(len(pairs),16)
            for suffix in suffixes:
                self.assertIn((family,'base'+suffix,'CO_CYC1_DRAW'+suffix),pairs)
                self.assertIn((family,'CO_CYC1'+suffix,'CO_CYC1_DRAW'+suffix),pairs)
                self.assertIn((family,'CO_CYC1'+suffix,'CO_CYC1_REV_CYCLERS'+suffix),pairs)
                self.assertIn((family,'CO_CYC1'+suffix,'CO_CYC1_MIDDLE'+suffix),pairs)

    def test_selected_input_directory_owns_all_default_report_paths(self):
        selected = self.root/'v2-inputs'
        primary = selected/'runs/primary-quality'
        for mode in ('quality', 'memory', 'memory-preflight'):
            a = report.parse_args([mode, '--inputs', str(selected)])
            self.assertEqual(a.output, selected/'reports')
            if mode == 'quality':
                self.assertEqual((a.wins, a.traces, a.stderr),
                                 (primary/'wins', primary/'traces', primary/'batch.err'))
            elif mode == 'memory':
                self.assertEqual((a.primary_wins, a.primary_stderr), (primary/'wins', primary/'batch.err'))

    def test_explicit_report_path_overrides_survive_selected_inputs(self):
        override = self.root/'separate-wins'
        a = report.parse_args(['quality', '--inputs', str(self.root/'v2'),
                               '--wins', str(override), '--output', str(self.root/'review')])
        self.assertEqual(a.wins, override)
        self.assertEqual(a.output, self.root/'review')
        self.assertEqual(a.traces, self.root/'v2/runs/primary-quality/traces')
        a = report.parse_args(['memory', '--inputs', str(self.root/'v2'),
                               '--primary-stderr', str(self.root/'actual.err')])
        self.assertEqual(a.primary_stderr, self.root/'actual.err')

    def test_pressure_count_and_fsl_are_separate(self):
        path = self.root / 'stderr'
        path.write_text(pool(fsl=7))
        p = report.cache_pressure(path)
        self.assertEqual(p['plan_pool']['refused_stores'], 3)
        self.assertEqual(p['fsl_refused_stores'], 7)
        self.assertTrue(p['fsl_bound'])
        self.assertEqual(len(p['fsl_warnings']), 1)
        self.assertFalse(p['problems'])

    def test_zero_pressure_is_known_but_not_a_stress_proof(self):
        path = self.root / 'stderr'
        path.write_text(pool(refused=0))
        p = report.cache_pressure(path)
        self.assertEqual(p['plan_pool']['refused_stores'], 0)
        self.assertEqual(p['fsl_refused_stores'], 0)
        self.assertFalse(p['problems'])

    def test_missing_summary_and_conflicting_counts_fail(self):
        path = self.root / 'stderr'
        self.assertTrue(report.cache_pressure(path)['problems'])
        path.write_text('[other] no exit stats\n')
        self.assertTrue(report.cache_pressure(path)['problems'])
        path.write_text(pool().replace('plan cache refused 3', 'plan cache refused 4'))
        self.assertTrue(report.cache_pressure(path)['problems'])

    def fixture(self, refused=3, budget=20480, fsl=0):
        inputs = self.root / 'inputs'; inputs.mkdir()
        primary = self.root / 'primary'; primary.mkdir()
        trial = self.root / 'trial'; trial.mkdir()
        jobs, maps = [], {}
        for off in range(2):
            name = f'cyclone_d3_s121000_off{off}'
            jobs.append(dict(name=name, seed=121000+off, game_index=off, games=1,
                             deck='decks/Unpredictable Cyclone/Unpredictable Cyclone.cod'))
            maps[name] = dict(family='memory_invariance', logical_cell='cyclone_d3_s121000')
            (primary / (name+'.wins')).write_text(f'{off} 4 abcdef0123456789\n')
            (primary / (name+'.units')).write_text(f'{off} {100+off}\n')
        measured = dict(name='cyclone_d3_s121000', seed=121000, game_index=0, games=2)
        report.write_json(inputs/'primary-quality.json', dict(jobs=jobs))
        report.write_json(inputs/'primary-quality-plan.json', dict(job_map=maps, env=FIDELITY_ENV, threads=24))
        report.write_json(inputs/'tiny.json', dict(jobs=[measured]))
        (trial/'cyclone_d3_s121000.wins').write_text('0 4 abcdef0123456789\n1 4 abcdef0123456789\n')
        (trial/'cyclone_d3_s121000.units').write_text('0 100\n1 101\n')
        primary_err = self.root/'primary.err'; primary_err.write_text(pool(budget=100000,refused=0))
        trial_err = self.root/'tiny.err'; trial_err.write_text(pool(budget=budget,refused=refused,fsl=fsl))
        normal12 = self.root/'normal12'; normal12.mkdir()
        for path in trial.iterdir():
            (normal12/path.name).write_bytes(path.read_bytes())
        normal12_err = self.root/'normal12.err'; normal12_err.write_text(pool(budget=100000, refused=0))
        report.write_json(inputs/'final-runtime-plan.json', dict(memory_comparisons=[dict(
            name='memory_t12_tiny', argv=['mtg','--batch',str(inputs/'tiny.json')],
            stdout=str(trial/'batch.log'),stderr=str(trial_err), env=FIDELITY_ENV), dict(
            name='memory_t12_both_reference_sets', argv=['mtg','--batch',str(inputs/'tiny.json'),'--threads','12'],
            stdout=str(normal12/'batch.log'),stderr=str(normal12_err), env=FIDELITY_ENV)]))
        return argparse.Namespace(inputs=inputs, primary_wins=primary, primary_stderr=primary_err,
                                  run=['memory_t12_tiny='+str(trial), 'memory_t12_both_reference_sets='+str(normal12)],stderr=[],output=self.root/'reports')

    def run_memory(self, a):
        with contextlib.redirect_stdout(io.StringIO()):
            rc = report.memory(a)
        return rc, report.read_json(a.output/'memory-report.json')

    def test_chunk_identity_and_positive_tiny_pressure_pass(self):
        rc, result = self.run_memory(self.fixture())
        self.assertEqual(rc, 0)
        self.assertTrue(result['proof_pass'])
        self.assertEqual(result['baseline'], 'primary normal24, chunked')
        self.assertTrue(result['normal12_reference_proven'])
        self.assertEqual(result['comparisons']['memory_t12_tiny']['compared'], 2)

    def test_full_memory_cannot_omit_independent_normal12(self):
        a = self.fixture(); path = a.inputs/'final-runtime-plan.json'
        schedule = report.read_json(path)
        schedule['memory_comparisons'] = [schedule['memory_comparisons'][0]]
        report.write_json(path, schedule)
        a.run = a.run[:1]
        rc, result = self.run_memory(a)
        self.assertEqual(rc, 1)
        self.assertFalse(result['normal12_reference_proven'])
        self.assertFalse(result['proof_pass'])

    def test_equal_without_binding_is_inconclusive(self):
        rc, result = self.run_memory(self.fixture(refused=0))
        self.assertEqual(rc, 1)
        self.assertTrue(result['exact'])
        self.assertFalse(result['proof_pass'])
        self.assertTrue(result['pressure_failures'])

    def test_wrong_budget_cannot_claim_tiny20mb_proof(self):
        rc, result = self.run_memory(self.fixture(budget=100000))
        self.assertEqual(rc, 1)
        self.assertFalse(result['tiny12_shared_plan_pressure_proven'])

    def test_fsl_bound_is_recorded_separately_from_observed_equality(self):
        rc, result = self.run_memory(self.fixture(fsl=8))
        self.assertEqual(rc, 0)
        self.assertTrue(result['exact'])
        self.assertTrue(result['cache_pressure']['memory_t12_tiny']['fsl_bound'])
        self.assertIn('not a general FSL invariance proof', result['cache_pressure']['memory_t12_tiny']['fsl_caveat'])

    def test_missing_exit_stats_fail_even_with_all_outcomes(self):
        a = self.fixture()
        Path(report.read_json(a.inputs/'final-runtime-plan.json')['memory_comparisons'][0]['stderr']).unlink()
        rc, result = self.run_memory(a)
        self.assertEqual(rc, 2)
        self.assertFalse(result['complete'])

    def test_unit_and_digest_changes_are_retained(self):
        a = self.fixture(); trial = self.root/'trial'
        (trial/'cyclone_d3_s121000.units').write_text('0 100\n1 102\n')
        (trial/'cyclone_d3_s121000.wins').write_text('0 4 abcdef0123456789\n1 4 0000000000000000\n')
        rc, result = self.run_memory(a)
        self.assertEqual(rc, 1)
        self.assertEqual(result['differences'][0]['changed'], ['digest','units'])

    def test_missing_units_are_incomplete_not_zero(self):
        a = self.fixture()
        (self.root/'trial/cyclone_d3_s121000.units').unlink()
        rc, result = self.run_memory(a)
        self.assertEqual(rc, 2)
        self.assertTrue(result['problems'])

    def preflight_fixture(self, fsl=0):
        a = self.fixture(refused=0, fsl=fsl)
        six = self.root/'six'; six.mkdir()
        for path in (self.root/'trial').iterdir():
            (six/path.name).write_bytes(path.read_bytes())
        shape = dict(name='cyclone_shapes',seed=107000,game_index=0,games=1)
        (six/'cyclone_shapes.wins').write_text('0 5 abcdef0123456789\n')
        (six/'cyclone_shapes.units').write_text('0 222\n')
        shared = report.read_json(a.inputs/'tiny.json')['jobs']
        report.write_json(a.inputs/'six.json',dict(jobs=shared+[shape]))
        variants=[]
        for name,manifest,wins,err in (
            ('memory_t1_normal',a.inputs/'tiny.json',self.root/'trial',self.root/'primary.err'),
            ('memory_t6_both_reference_sets',a.inputs/'six.json',six,self.root/'tiny.err')):
            variants.append(dict(name=name,argv=['mtg','--batch',str(manifest)],
                                 stdout=str(wins/'batch.log'),stderr=str(err), env=FIDELITY_ENV))
        report.write_json(a.inputs/'final-runtime-plan.json',dict(memory_comparisons=variants))
        for path in (a.inputs/'primary-quality.json',a.inputs/'primary-quality-plan.json'):
            path.unlink()
        for path in (self.root/'primary').iterdir():
            path.unlink()
        (self.root/'primary').rmdir()
        return argparse.Namespace(inputs=a.inputs,run=[
            'memory_t1_normal='+str(self.root/'trial'),
            'memory_t6_both_reference_sets='+str(six)],stderr=[],output=a.output)

    def run_preflight(self, a):
        with contextlib.redirect_stdout(io.StringIO()):
            rc = report.memory_preflight(a)
        return rc,report.read_json(a.output/'memory-preflight-report.json')

    def test_preflight_needs_no_primary_inputs_or_outputs(self):
        rc,result = self.run_preflight(self.preflight_fixture())
        self.assertEqual(rc,0)
        self.assertTrue(result['preflight_pass'])
        self.assertEqual(result['shared_compared'],2)
        self.assertEqual(result['six_only_expected'],1)
        self.assertNotIn('tiny12_shared_plan_pressure_proven',result)

    def test_preflight_fsl_binding_requires_review_even_if_equal(self):
        rc,result = self.run_preflight(self.preflight_fixture(fsl=9))
        self.assertEqual(rc,1)
        self.assertTrue(result['observed_exact'])
        self.assertFalse(result['preflight_pass'])
        pressure=result['cache_pressure']['memory_t6_both_reference_sets']
        self.assertEqual(pressure['fsl_refused_stores'],9)
        self.assertTrue(pressure['fsl_warnings'])

    def test_preflight_shared_unit_difference_blocks(self):
        a=self.preflight_fixture()
        (self.root/'six/cyclone_d3_s121000.units').write_text('0 100\n1 102\n')
        rc,result=self.run_preflight(a)
        self.assertEqual(rc,1)
        self.assertEqual(result['differences'][0]['changed'],['units'])

    def test_preflight_missing_shapes_file_is_incomplete(self):
        a=self.preflight_fixture()
        (self.root/'six/cyclone_shapes.units').unlink()
        rc,result=self.run_preflight(a)
        self.assertEqual(rc,2)
        self.assertFalse(result['complete'])

    def test_preflight_all_ordinary_fd_records_block_even_leaf_estimate(self):
        a = self.preflight_fixture()
        for leaf in ('none', '4'):
            with self.subTest(leaf=leaf):
                (self.root/'tiny.err').write_text(pool(refused=0) +
                    f'[fd-diverge] job=cyclone_shapes seed=107000 gi=0 predicted_win=4 leaf_est={leaf}\n')
                rc, result = self.run_preflight(a)
                self.assertEqual(rc, 1)
                self.assertTrue(result['observed_exact'])
                self.assertFalse(result['preflight_pass'])
                self.assertFalse(result['fidelity_clean'])
                self.assertEqual(len(result['fidelity']['memory_t6_both_reference_sets']['fatal']), 1)

    def test_preflight_nonconv_without_attribution_also_blocks(self):
        a = self.preflight_fixture()
        (self.root/'tiny.err').write_text(pool(refused=0) + '[nonconv] malformed diagnostic\n')
        rc, result = self.run_preflight(a)
        self.assertEqual(rc, 1)
        self.assertFalse(result['fidelity_clean'])

    def test_preflight_disabled_diagnostics_are_incomplete_not_clean(self):
        a = self.preflight_fixture()
        path = a.inputs/'final-runtime-plan.json'
        schedule = report.read_json(path)
        schedule['memory_comparisons'][0]['env'].pop('MTG_FD_ORACLE')
        report.write_json(path, schedule)
        rc, result = self.run_preflight(a)
        self.assertEqual(rc, 2)
        self.assertFalse(result['complete'])
        self.assertFalse(result['preflight_pass'])

    def test_full_memory_cannot_pass_equal_outcomes_with_ordinary_fd(self):
        a = self.fixture()
        (self.root/'tiny.err').write_text(pool() + '[fd-diverge] seed=121000 gi=0 leaf_est=none\n')
        rc, result = self.run_memory(a)
        self.assertEqual(rc, 1)
        self.assertTrue(result['exact'])
        self.assertFalse(result['proof_pass'])
        self.assertFalse(result['fidelity_clean'])

    def test_mixed_primary_fidelity_separates_blind_and_memory_records(self):
        jobs = [dict(name='ordinary', seed=100, game_index=0, games=1,
                     deck='decks/Unpredictable Cyclone/Unpredictable Cyclone.cod'),
                dict(name='blind', seed=200, game_index=0, games=1, blind_salt=1,
                     deck='decks/Unpredictable Cyclone/Unpredictable Cyclone.cod')]
        maps = {'ordinary': {'family': 'memory_invariance'}, 'blind': {'family': 'cast_q64_blind'}}
        path = self.root/'primary-diagnostics.err'
        path.write_text('[fd-diverge] job=blind seed=200 gi=0 predicted_win=4 leaf_est=none\n')
        inventory = report.memory_diagnostics(path, {'env': FIDELITY_ENV}, jobs, maps)
        self.assertTrue(inventory['clean'])
        self.assertEqual(inventory['items'][0]['classification'], 'blind_forecast_requires_ordinary_world_control')
        path.write_text(path.read_text() + '[fd-diverge] job=ordinary seed=100 gi=0 predicted_win=4 leaf_est=4\n')
        inventory = report.memory_diagnostics(path, {'env': FIDELITY_ENV}, jobs, maps)
        self.assertFalse(inventory['clean'])
        self.assertEqual(len(inventory['fatal']), 1)
        self.assertEqual(inventory['fatal'][0]['classification'], 'leaf_estimate_warning')
        path.write_text('[fd-diverge] unparsed\n')
        self.assertFalse(report.memory_diagnostics(path, {'env': FIDELITY_ENV}, jobs, maps)['clean'])


if __name__ == '__main__':
    unittest.main()
