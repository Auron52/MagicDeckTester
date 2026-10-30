#!/usr/bin/env python3
"""Synthetic parser/report checks only: never starts the engine."""
import argparse
import contextlib
import importlib.util
import io
import json
import tempfile
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('cyclone_report', Path(__file__).with_name('report_final_results.py'))
report = importlib.util.module_from_spec(spec)
spec.loader.exec_module(report)


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
            jobs.append(dict(name=name, seed=121000+off, game_index=off, games=1))
            maps[name] = dict(family='memory_invariance', logical_cell='cyclone_d3_s121000')
            (primary / (name+'.wins')).write_text(f'{off} 4 abcdef0123456789\n')
            (primary / (name+'.units')).write_text(f'{off} {100+off}\n')
        measured = dict(name='cyclone_d3_s121000', seed=121000, game_index=0, games=2)
        report.write_json(inputs/'primary-quality.json', dict(jobs=jobs))
        report.write_json(inputs/'primary-quality-plan.json', dict(job_map=maps))
        report.write_json(inputs/'tiny.json', dict(jobs=[measured]))
        (trial/'cyclone_d3_s121000.wins').write_text('0 4 abcdef0123456789\n1 4 abcdef0123456789\n')
        (trial/'cyclone_d3_s121000.units').write_text('0 100\n1 101\n')
        primary_err = self.root/'primary.err'; primary_err.write_text(pool(budget=100000,refused=0))
        trial_err = self.root/'tiny.err'; trial_err.write_text(pool(budget=budget,refused=refused,fsl=fsl))
        report.write_json(inputs/'final-runtime-plan.json', dict(memory_comparisons=[dict(
            name='memory_t12_tiny', argv=['mtg','--batch',str(inputs/'tiny.json')],
            stdout=str(trial/'batch.log'),stderr=str(trial_err))]))
        return argparse.Namespace(inputs=inputs, primary_wins=primary, primary_stderr=primary_err,
                                  run=['memory_t12_tiny='+str(trial)],stderr=[],output=self.root/'reports')

    def run_memory(self, a):
        with contextlib.redirect_stdout(io.StringIO()):
            rc = report.memory(a)
        return rc, report.read_json(a.output/'memory-report.json')

    def test_chunk_identity_and_positive_tiny_pressure_pass(self):
        rc, result = self.run_memory(self.fixture())
        self.assertEqual(rc, 0)
        self.assertTrue(result['proof_pass'])
        self.assertEqual(result['comparisons']['memory_t12_tiny']['compared'], 2)

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
                                 stdout=str(wins/'batch.log'),stderr=str(err)))
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


if __name__ == '__main__':
    unittest.main()
