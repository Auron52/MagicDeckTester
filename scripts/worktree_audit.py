#!/usr/bin/env python3
"""Audit (and optionally prune) this repo's git worktrees.

WHY. Agent worktrees pile up -- 40 of them, 161 GB, on 2026-10-06 -- and some get created under
/tmp, which is the container's overlay filesystem: a stopped or replaced container takes them with
it (that is how a set of hand-played references was lost on 2026-10-05). The rule (CLAUDE.md) is:
worktrees live under `.claude/worktrees/` on the persistent host mount, never /tmp; and ones with
NOTHING TO ADD get cleaned up occasionally. This tool is the "nothing to add" test, made explicit.

A worktree has NOTHING TO ADD when ALL of:
  * clean       -- no modified/staged files and no untracked files (ignored files do not count:
                   build/ and logs/ are rebuildable scratch);
  * pushed      -- every commit reachable from its HEAD is on SOME origin ref
                   (`git rev-list HEAD --not --remotes=origin` is empty), OR every one of its commits
                   is already on the main branch BY PATCH (`git cherry --main-ref HEAD` has no '+'):
                   agent branches are rebased into the main line, so their hashes never match. Either
                   way the work is retrievable from the remote even if the directory vanishes;
  * not in use  -- no process has its working directory inside it;
  * old enough  -- its git index has not been touched for --min-age-days (default 2), so a worktree
                   another session set up an hour ago is not swept out from under it.
Everything else is KEEP, with the reason shown. The main checkout is never a candidate.

Usage:
  python3 scripts/worktree_audit.py                      # table + summary, read-only
  python3 scripts/worktree_audit.py --no-size            # skip du (the host mount is slow)
  python3 scripts/worktree_audit.py --prune-stale        # print what WOULD be removed
  python3 scripts/worktree_audit.py --prune-stale --yes  # remove them (git worktree remove --force
                                                         #   + git worktree prune); only STALE ones
  python3 scripts/worktree_audit.py --json               # machine-readable

Removal is `git worktree remove`, so the branch itself is untouched; it stays retrievable from the
remote (that is what "pushed" guarantees) and can be checked out again with `git worktree add`.
"""
import argparse
import json
import os
import subprocess
import sys
import time

ROOT = subprocess.run(['git', 'rev-parse', '--show-toplevel'], capture_output=True, text=True,
                      check=True).stdout.strip()


def git(args, cwd=ROOT):
    p = subprocess.run(['git'] + args, cwd=cwd, capture_output=True, text=True)
    return p.returncode, p.stdout.strip(), p.stderr.strip()


def worktrees():
    rc, out, _ = git(['worktree', 'list', '--porcelain'])
    items, cur = [], {}
    for line in out.splitlines() + ['']:
        if not line:
            if cur:
                items.append(cur)
            cur = {}
            continue
        k, _, v = line.partition(' ')
        if k == 'worktree':
            cur['path'] = v
        elif k == 'HEAD':
            cur['head'] = v
        elif k == 'branch':
            cur['branch'] = v.replace('refs/heads/', '')
        elif k in ('detached', 'bare', 'prunable', 'locked'):
            cur[k] = v or True
    return items


def cwd_users():
    users = {}
    for pid in os.listdir('/proc'):
        if not pid.isdigit():
            continue
        try:
            cwd = os.readlink(f'/proc/{pid}/cwd')
        except OSError:
            continue
        users.setdefault(cwd, []).append(int(pid))
    return users


def in_use(path, users):
    path = path.rstrip('/') + '/'
    return sorted(p for cwd, pids in users.items() for p in pids if (cwd + '/').startswith(path))


def du_gb(path):
    p = subprocess.run(['du', '-sk', path], capture_output=True, text=True)
    try:
        return int(p.stdout.split()[0]) / (1024 * 1024)
    except (IndexError, ValueError):
        return None


def audit(min_age_days, want_size, main_ref='origin/phase-1-2-deck-analyzer'):
    users = cwd_users()
    rows = []
    for w in worktrees():
        path = w.get('path', '')
        row = {'path': path, 'branch': w.get('branch') or ('(detached)' if w.get('detached') else '?'),
               'head': (w.get('head') or '')[:8], 'main': path == ROOT}
        if not os.path.isdir(path):
            row.update(missing=True, verdict='MISSING', reason='directory gone (git worktree prune)')
            rows.append(row)
            continue
        rc, st, _ = git(['status', '--porcelain', '--untracked-files=normal'], cwd=path)
        lines = [l for l in st.splitlines() if l.strip()]
        row['modified'] = sum(1 for l in lines if not l.startswith('??'))
        row['untracked'] = sum(1 for l in lines if l.startswith('??'))
        rc, unp, _ = git(['rev-list', '--count', 'HEAD', '--not', '--remotes=origin'], cwd=path)
        row['unpushed'] = int(unp) if rc == 0 and unp.isdigit() else -1
        # PATCH-equivalence against the main branch: an agent branch is usually rebased/cherry-picked
        # into the main line, so its commits are "not on origin" BY HASH while every change in them
        # is. `git cherry` marks a commit '+' only when no upstream commit has the same patch-id.
        rc, ch, _ = git(['cherry', main_ref, 'HEAD'], cwd=path)
        row['unlanded'] = sum(1 for l in ch.splitlines() if l.startswith('+')) if rc == 0 else -1
        row['pids'] = in_use(path, users)
        rc, gd, _ = git(['rev-parse', '--git-dir'], cwd=path)
        idx = os.path.join(gd if os.path.isabs(gd) else os.path.join(path, gd), 'index')
        try:
            row['age_days'] = (time.time() - os.stat(idx).st_mtime) / 86400.0
        except OSError:
            row['age_days'] = None
        row['on_tmp'] = any(path.startswith(t.rstrip('/') + '/') for t in
                            ('/tmp', os.environ.get('TMPDIR') or '/nonexistent'))
        row['size_gb'] = du_gb(path) if want_size else None
        reasons = []
        if row['main']:
            reasons.append('main checkout')
        if row['modified']:
            reasons.append(f"{row['modified']} modified")
        if row['untracked']:
            reasons.append(f"{row['untracked']} untracked")
        if row['unpushed'] != 0 and row['unlanded'] != 0:
            reasons.append(f"{row['unpushed']} commit(s) not on origin, {row['unlanded']} not landed by patch"
                           if row['unpushed'] > 0 else 'unpushed: unknown')
        elif row['unpushed'] > 0 and row['unlanded'] == 0:
            row['landed'] = True   # every patch is on the main branch under another hash
        if row['pids']:
            reasons.append(f"in use by pid {row['pids'][0]}")
        if row['age_days'] is not None and row['age_days'] < min_age_days:
            reasons.append(f"touched {row['age_days']:.1f} d ago (< {min_age_days})")
        row['verdict'] = 'KEEP' if reasons else 'STALE'
        row['reason'] = '; '.join(reasons) if reasons else (
            f"nothing to add ({row['unpushed']} commits, every patch already on {main_ref})"
            if row.get('landed') else 'nothing to add')
        rows.append(row)
    return rows


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--min-age-days', type=float, default=2.0)
    ap.add_argument('--no-size', action='store_true', help='skip du (slow on the host mount)')
    ap.add_argument('--prune-stale', action='store_true', help='list (or with --yes, remove) STALE worktrees')
    ap.add_argument('--yes', action='store_true', help='actually remove; without it --prune-stale is a dry run')
    ap.add_argument('--json', action='store_true')
    ap.add_argument('--main-ref', default='origin/phase-1-2-deck-analyzer',
                    help='the line agent branches land on (patch-equivalence test)')
    a = ap.parse_args()
    rows = audit(a.min_age_days, want_size=not a.no_size, main_ref=a.main_ref)
    if a.json:
        print(json.dumps(rows, indent=1))
    else:
        print(f"{'VERDICT':7s} {'SIZE':>7s} {'AGE':>6s} {'MOD':>3s} {'UNT':>3s} {'UNP':>3s} {'UNL':>3s} {'BRANCH':40s} PATH  -- reason")
        for r in rows:
            size = f"{r['size_gb']:.1f}G" if r.get('size_gb') is not None else '-'
            age = f"{r['age_days']:.0f}d" if r.get('age_days') is not None else '-'
            tmp = ' [ON /tmp]' if r.get('on_tmp') else ''
            print(f"{r['verdict']:7s} {size:>7s} {age:>6s} {r.get('modified', 0):3d} {r.get('untracked', 0):3d} "
                  f"{r.get('unpushed', 0):3d} {r.get('unlanded', 0):3d} {r['branch'][:40]:40s} {r['path']}{tmp}  -- {r['reason']}")
        stale = [r for r in rows if r['verdict'] == 'STALE']
        keep = [r for r in rows if r['verdict'] == 'KEEP' and not r['main']]
        tot = sum(r['size_gb'] or 0 for r in stale)
        print(f"\n{len(stale)} STALE (nothing to add, {tot:.1f} GB), {len(keep)} KEEP, "
              f"{sum(1 for r in rows if r.get('on_tmp'))} on /tmp (NOT persistent), "
              f"{sum(1 for r in rows if r.get('missing'))} missing")
    if a.prune_stale:
        stale = [r for r in rows if r['verdict'] == 'STALE']
        for r in stale:
            if a.yes:
                # REFERENCE SAFETY gate (CLAUDE.md): never remove a worktree holding a reference game
                # that exists nowhere else, whatever the STALE verdict above concluded.
                chk = subprocess.run([sys.executable, os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                      'check_references_safe.py'), r['path']], capture_output=True, text=True)
                if chk.returncode != 0:
                    print(f"REFUSED {r['path']}: unprotected reference games (scripts/check_references_safe.py):\n{chk.stdout}{chk.stderr}")
                    continue
                rc, out, err = git(['worktree', 'remove', '--force', r['path']])
                print(f"removed {r['path']}" if rc == 0 else f"FAILED {r['path']}: {err}")
            else:
                print(f"would remove: {r['path']}  ({r['branch']}, {r['reason']})")
        if a.yes:
            git(['worktree', 'prune'])
        elif stale:
            print("dry run -- add --yes to remove")
    return 0


if __name__ == '__main__':
    sys.exit(main())
