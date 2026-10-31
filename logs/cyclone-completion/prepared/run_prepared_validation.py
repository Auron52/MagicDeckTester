#!/usr/bin/env python3
"""Seal and run one prepared validation stage after final PGO is fresh.

From the primary checkout:
  python3 build/scratch/cyclone-finish/run_prepared_validation.py primary

This blocks until its engine exits, keeps stdout/stderr in the build-volume run
directory, and writes PID/status/DONE or STOPPED markers. It never launches a
stale binary, modifies source, overwrites an existing result, or uses a timeout.
The root agent still owns required progress communication and status timers.
"""
import argparse
import datetime
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path


HELPERS = Path(__file__).resolve().parent
BASE = next(p for p in HELPERS.parents
            if (p / 'CLAUDE.md').is_file() and (p / '.git').is_dir())
SCRATCH = BASE / 'build/scratch/cyclone-finish'


def now():
    return datetime.datetime.now(datetime.timezone.utc).isoformat()


def launch_environment(inherited, unset, configured):
    env = dict(inherited)
    for key in unset:
        env.pop(key, None)
    # Intentional memory pins and tiny/unbounded capacities apply only after
    # inherited process settings have been cleared.
    env.update(configured)
    env['GIT_OPTIONAL_LOCKS'] = '0'
    return env


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('stage', choices=['primary', 'cost', 'verify', 'memory1', 'memory6', 'memory-tiny12', 'memory-unbounded6'])
    ap.add_argument('--worktree', type=Path, default=BASE / '.claude/worktrees/cyclone-finish')
    ap.add_argument('--inputs', type=Path, default=SCRATCH / 'final-pgo-inputs')
    ap.add_argument('--prunes', type=Path, default=SCRATCH / 'final-prunes/phasec/manifest.json')
    ap.add_argument('--bin', type=Path)
    ap.add_argument('--dry-run', action='store_true', help='seal/check then print stage plan without launching')
    a = ap.parse_args()
    root, inputs = a.worktree.resolve(), a.inputs.resolve()
    binary = (a.bin or root / 'build/PGO/mtg').resolve()
    read_env = {**os.environ, 'GIT_OPTIONAL_LOCKS': '0'}
    own = json.loads((inputs / 'run-plan.json').read_text())
    sampler = own.get('qc1_sampler_provenance')
    if sampler is None:
        raise ValueError('QC1 sampler provenance absent; regenerate final inputs with original completed sample')
    for name, expected in sampler['recorded']['inputs'].items():
        if hashlib.sha256((root / name).read_bytes()).hexdigest() != expected:
            raise ValueError('QC1 original sampling input changed: ' + name)
    # Sealing independently verifies clean source, matching PGO stamp and fresh
    # pruning provenance. The composed manifest must retain its prepared jobs.
    subprocess.run([sys.executable, str(HELPERS / 'compose_final_quality.py'),
                    '--inputs', str(inputs), '--prunes', str(a.prunes.resolve()),
                    '--worktree', str(root), '--bin', str(binary), '--threads', '12', '--seal'],
                   check=True, env=read_env)
    schedule = json.loads((inputs / 'final-runtime-plan.json').read_text())
    if a.stage == 'primary':
        plan = schedule['primary_quality']
    elif a.stage == 'cost':
        plan, = schedule['quiet_cost']
        # A cost comparison is meaningless alongside another engine pool. This
        # checks visible engines; the root must also ensure host load is quiet.
        running = []
        for p in Path('/proc').iterdir():
            if not p.name.isdigit():
                continue
            try:
                name = (p / 'comm').read_text().strip()
            except (OSError, PermissionError):
                continue
            if name in ('mtg', 'mtg.exe', 'mtg-test', 'mtg-test.exe'):
                # A viewer engine asleep waiting for the human is not a load.
                # Inspect worker states too: a busy pool's main thread sleeps
                # while its worker threads run.
                try:
                    active = any(task.joinpath('stat').read_text().rsplit(')', 1)[1].split()[0] in ('R', 'D')
                                 for task in p.joinpath('task').iterdir())
                except (OSError, PermissionError, IndexError):
                    active = False
                if active:
                    running.append(int(p.name))
        if running:
            raise ValueError('Cost gate requires a quiet box; visible engine PIDs: ' + str(running))
    elif a.stage == 'verify':
        plan, = schedule['verify_after_main_and_cost']
    else:
        wanted = {'memory1': 'memory_t1_normal', 'memory6': 'memory_t6_both_reference_sets',
                  'memory-tiny12': 'memory_t12_tiny', 'memory-unbounded6': 'memory_t6_unbounded'}[a.stage]
        plan, = [p for p in schedule['memory_comparisons'] if p['name'] == wanted]
    if a.dry_run:
        print(json.dumps(plan, indent=2))
        return
    stdout, stderr = Path(plan['stdout']), Path(plan['stderr'])
    run = stdout.parent
    if stdout.exists() or stderr.exists() or (run / 'status.json').exists():
        raise ValueError('Refusing to overwrite an existing run: ' + str(run))
    run.mkdir(parents=True, exist_ok=True)
    if a.stage == 'primary':
        # The dedicated pruning reporter consumes its own standard paths. Link
        # those paths to this shared unique-name pool instead of copying results
        # or forcing a second pruning invocation.
        for old, target in ((a.prunes.resolve().parent / 'wins', run / 'wins'),
                            (a.prunes.resolve().parent / 'traces', run / 'traces'),
                            (a.prunes.resolve().parent / 'batch.log', stdout),
                            (a.prunes.resolve().parent / 'batch.err', stderr)):
            if old.is_symlink():
                if old.resolve() != target.resolve():
                    raise ValueError('Existing pruning result alias points elsewhere: ' + str(old))
            elif old.exists():
                raise ValueError('Refusing to replace existing pruning results: ' + str(old))
            else:
                old.symlink_to(target, target_is_directory=old.name in ('wins', 'traces'))
    argv = list(plan['argv'])
    argv[0] = str(binary)
    for flag in ('--game-log-dir', '--game-trace-dir'):
        if flag in argv:
            Path(argv[argv.index(flag) + 1]).mkdir(parents=True, exist_ok=True)
    env = launch_environment(os.environ, schedule['primary_quality']['unset_before_run'], plan['env'])
    record = dict(stage=a.stage, name=plan['name'] if 'name' in plan else 'primary-quality',
                  owner_pid=os.getpid(), started=now(), argv=argv, env=plan['env'],
                  input_manifest=argv[argv.index('--batch') + 1],
                  source_tree=schedule['primary_quality']['src_tree'],
                  binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),
                  status='STARTING', stdout=str(stdout), stderr=str(stderr))
    status = run / 'status.json'
    status.write_text(json.dumps(record, indent=2) + '\n')
    with stdout.open('x') as out, stderr.open('x') as err:
        child = subprocess.Popen(argv, cwd=plan['cwd'], env=env, stdout=out, stderr=err)
        record.update(status='RUNNING', child_pid=child.pid)
        status.write_text(json.dumps(record, indent=2) + '\n')
        print(json.dumps(dict(stage=a.stage, child_pid=child.pid, status=str(status)), indent=2), flush=True)
        rc = child.wait()
    if rc == 0 and a.stage == 'verify':
        report = run / 'verify-exercise.json'
        with report.open('x') as out:
            rc = subprocess.run([sys.executable, str(HELPERS / 'check_small_verify.py'), str(stdout), str(stderr)],
                                stdout=out, env=read_env).returncode
    record.update(status='DONE' if rc == 0 else 'STOPPED', returncode=rc, finished=now())
    status.write_text(json.dumps(record, indent=2) + '\n')
    (run / record['status']).write_text(json.dumps(record, indent=2) + '\n')
    raise SystemExit(rc)


if __name__ == '__main__':
    main()
