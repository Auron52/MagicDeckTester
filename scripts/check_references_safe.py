#!/usr/bin/env python3
"""Is every hand-played reference game SAFE? Run before removing/pruning/recreating ANY worktree or
clone, and at session close.

WHY. The play viewer saves the user's hand-played games under <checkout>/references/. On 2026-10-05
a week of them was lost because a viewer had been started from a scratch worktree that was then
wiped with the container. A reference is SAFE only when its exact bytes are on a branch that lives
on origin (or on the play viewer's references-autosave branch). Anything else is one
`git worktree remove`, `git clean`, or container rebuild away from being gone forever.

WHAT. For the primary checkout and every linked worktree (`git worktree list --porcelain`), or just
the PATHs given, list each file under references/ that is untracked or modified vs HEAD and whose
blob is NOT present in any of:
  * refs/remotes/origin/*        (all of origin's branches, as last fetched)
  * refs/heads/references-autosave and refs/remotes/origin/references-autosave
Exit 1 if any such file exists (each is printed with the command that would protect it), else 0.

  python3 scripts/check_references_safe.py              # every worktree of this repo
  python3 scripts/check_references_safe.py <path> ...   # only these checkouts/worktrees
  python3 scripts/check_references_safe.py --fetch      # `git fetch origin` first (fresher refs)

A file that is ONLY on the local references-autosave branch counts as present but is reported as a
WARNING (the branch is in the shared object store, so it survives a worktree removal -- not a
container rebuild). Push it: `git push origin references-autosave`.
"""
import os
import subprocess
import sys

AUTOSAVE = "references-autosave"


def git(cwd, *args, check=True):
    r = subprocess.run(["git", *args], cwd=cwd, capture_output=True)
    if check and r.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} (in {cwd}) failed: {r.stderr.decode(errors='replace').strip()}")
    return r


def worktrees(any_checkout):
    out = git(any_checkout, "worktree", "list", "--porcelain").stdout.decode()
    trees = []
    for block in out.split("\n\n"):
        wt = {}
        for line in block.splitlines():
            k, _, v = line.partition(" ")
            wt[k] = v or True
        if "worktree" in wt and "bare" not in wt:
            trees.append(wt["worktree"])
    return trees


def blobs_in(repo, ref):
    """Set of blob ids under references/ at `ref` (empty if the ref does not exist)."""
    if git(repo, "rev-parse", "--verify", "-q", ref, check=False).returncode != 0:
        return set()
    out = git(repo, "ls-tree", "-r", "-z", ref, "--", "references").stdout.decode(errors="replace")
    s = set()
    for ent in out.split("\0"):
        if ent:
            s.add(ent.split("\t", 1)[0].split()[2])
    return s


def at_risk_files(tree):
    """Untracked or modified (not deleted) files under references/ in worktree `tree`."""
    out = git(tree, "status", "--porcelain=v1", "-z", "--untracked-files=all", "--", "references").stdout
    ents = out.decode(errors="replace").split("\0")
    files, i = [], 0
    while i < len(ents):
        e = ents[i]
        i += 1
        if not e:
            continue
        xy, p = e[:2], e[3:]
        if xy[0] in "RC":
            i += 1
        if "D" in xy:
            continue
        if os.path.isfile(os.path.join(tree, p)):
            files.append(p)
    return files


def main(argv):
    do_fetch = "--fetch" in argv
    paths = [a for a in argv if not a.startswith("--")]
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    anchor = paths[0] if paths else here
    if do_fetch:
        r = git(anchor, "fetch", "--quiet", "origin", check=False)
        if r.returncode != 0:
            print("WARNING: git fetch origin failed; using the remote refs as last fetched:",
                  r.stderr.decode(errors="replace").strip())

    trees = [os.path.abspath(p) for p in paths] if paths else worktrees(anchor)
    unsafe, warn, scanned = [], [], 0
    remote_cache = {}
    for tree in trees:
        if not os.path.isdir(tree):
            print(f"skip: {tree} (worktree directory is missing -- prunable)")
            continue
        top = git(tree, "rev-parse", "--show-toplevel").stdout.decode().strip()
        common = os.path.realpath(git(tree, "rev-parse", "--path-format=absolute",
                                      "--git-common-dir").stdout.decode().strip())
        if common not in remote_cache:
            refs = git(top, "for-each-ref", "--format=%(refname)", "refs/remotes/origin").stdout.decode().split()
            remote = set()
            for ref in refs:
                if ref.endswith("/HEAD"):
                    continue
                remote |= blobs_in(top, ref)
            remote_cache[common] = (remote, blobs_in(top, "refs/heads/" + AUTOSAVE))
        remote, local_autosave = remote_cache[common]
        files = at_risk_files(top)
        scanned += 1
        # COMMITTED but never pushed: safe from `git worktree remove` while a BRANCH holds it (the
        # branch lives in the shared repo), lost to gc if HEAD is detached.
        branch = git(top, "symbolic-ref", "-q", "--short", "HEAD", check=False).stdout.decode().strip()
        out = git(top, "ls-tree", "-r", "-z", "HEAD", "--", "references", check=False).stdout.decode(errors="replace")
        for ent in out.split("\0"):
            if not ent:
                continue
            meta, p = ent.split("\t", 1)
            if p in files:
                continue
            blob = meta.split()[2]
            if blob in remote or blob in local_autosave:
                continue
            if branch:
                warn.append(f"{os.path.join(top, p)}  [committed on local branch '{branch}', NOT on origin -- push it]")
            else:
                unsafe.append((top, p + "  [committed on a DETACHED HEAD only]"))
        for f in files:
            blob = git(top, "hash-object", "--", os.path.join(top, f)).stdout.decode().strip()
            full = os.path.join(top, f)
            if blob in remote:
                continue
            if blob in local_autosave:
                warn.append(full)
            else:
                unsafe.append((top, f))

    print(f"scanned {scanned} checkout(s)/worktree(s) for unprotected reference games")
    for full in warn:
        print(f"  WARN (only on LOCAL {AUTOSAVE}; run `git push origin {AUTOSAVE}`): {full}")
    if unsafe:
        print(f"UNSAFE: {len(unsafe)} reference file(s) exist ONLY in a working tree:")
        for top, f in unsafe:
            print(f"  {os.path.join(top, f)}")
        print("Protect them BEFORE removing anything -- commit (commit-only rule) and push, e.g.:")
        for top in sorted({t for t, _ in unsafe}):
            fs = " ".join(f"'{f.split('  [')[0]}'" for t, f in unsafe if t == top)
            print(f"  git -C '{top}' add -- {fs} && git -C '{top}' commit -m 'refs: protect user references' && git -C '{top}' push")
        return 1
    print("OK: every untracked/modified reference is on origin or on the autosave branch.")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv[1:]))
    except RuntimeError as e:
        print("ERROR:", e, file=sys.stderr)
        sys.exit(2)
