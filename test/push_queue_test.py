#!/usr/bin/env python3
"""End-to-end test of scripts/push_queue.py against a throwaway bare origin (no network, no real branch).

Covers: the pre-push hook refusing a gated push and passing an ungated one; join order; `wait` (75 while
not head, 0 at the head); `land` pushing the shared branch and passing the lease atomically; a stale
base with gated incoming commits refused (4); ungated incoming commits rebased over automatically;
a stale head reaped; requeue / leave; `hold` heartbeating around a command.

    python3 test/push_queue_test.py
"""
import os
import shutil
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.join(HERE, "..", "scripts", "push_queue.py")
HOOK = os.path.join(HERE, "..", ".githooks", "pre-push")
SHARED = "phase-1-2-deck-analyzer"
FAILS = []


def run(cwd, *cmd, env=None, ok=True):
    r = subprocess.run(list(cmd), cwd=cwd, capture_output=True, text=True, env=dict(os.environ, **(env or {})))
    if ok and r.returncode != 0:
        raise RuntimeError("%s failed in %s: %s%s" % (" ".join(cmd), cwd, r.stdout, r.stderr))
    return r


def git(cwd, *args, **kw):
    return run(cwd, "git", *args, **kw)


def pq(cwd, who, *args, env=None):
    e = {"MDT_PUSH_QUEUE_ID": who}
    e.update(env or {})
    return run(cwd, sys.executable, os.path.join(cwd, "scripts", "push_queue.py"), *args, env=e, ok=False)


def check(cond, what, r=None):
    print(("ok    " if cond else "FAIL  ") + what)
    if not cond:
        FAILS.append(what)
        if r is not None:
            print("      rc=%s\n      %s%s" % (r.returncode, r.stdout.replace("\n", "\n      "), r.stderr))


def commit(cwd, path, text, msg):
    p = os.path.join(cwd, path)
    os.makedirs(os.path.dirname(p) or cwd, exist_ok=True)
    with open(p, "a") as f:
        f.write(text + "\n")
    git(cwd, "add", path)
    git(cwd, "commit", "-q", "-m", msg)


def tip(cwd, ref):
    return git(cwd, "rev-parse", ref).stdout.strip()


def main():
    tmp = tempfile.mkdtemp(prefix="push_queue_test.")
    try:
        origin = os.path.join(tmp, "origin.git")
        git(tmp, "init", "-q", "--bare", origin)
        seed = os.path.join(tmp, "seed")
        git(tmp, "init", "-q", "-b", SHARED, seed)
        for c in (seed,):
            git(c, "config", "user.email", "t@t"), git(c, "config", "user.name", "t")
        os.makedirs(os.path.join(seed, "scripts")), os.makedirs(os.path.join(seed, ".githooks"))
        shutil.copy(SCRIPT, os.path.join(seed, "scripts", "push_queue.py"))
        shutil.copy(HOOK, os.path.join(seed, ".githooks", "pre-push"))
        os.chmod(os.path.join(seed, ".githooks", "pre-push"), 0o755)
        os.makedirs(os.path.join(seed, "test"))
        with open(os.path.join(seed, "test", "regression.sh"), "w") as f:      # a tier that runs a helper
            f.write('#!/bin/bash\nHERE=$(dirname "$0")\n# scripts/only_in_a_comment.py is not a call\n'
                    'python3 "$HERE/../scripts/tier_helper.py"\n')
        for n in ("tier_helper.py", "unrelated.py", "only_in_a_comment.py"):
            with open(os.path.join(seed, "scripts", n), "w") as f:
                f.write("print(1)\n")
        git(seed, "add", "scripts", ".githooks", "test")
        commit(seed, "README", "seed", "seed")
        git(seed, "remote", "add", "origin", origin)
        git(seed, "push", "-q", "origin", SHARED)

        clones = {}
        for name in ("b", "c"):
            d = os.path.join(tmp, name)
            git(tmp, "clone", "-q", "-b", SHARED, origin, d)
            git(d, "config", "user.email", "%s@t" % name), git(d, "config", "user.name", name)
            git(d, "checkout", "-q", "-b", "work-" + name)
            clones[name] = d
        b, c = clones["b"], clones["c"]

        # -- the hook (installed by join; install it in c by hand to test a bypass push first)
        r = pq(c, "C", "install-hook")
        check(r.returncode == 0 and git(c, "config", "core.hooksPath").stdout.strip() == ".githooks",
              "install-hook sets core.hooksPath", r)
        commit(c, "src/engine.cpp", "c1", "c: src change")
        r = git(c, "push", "origin", "HEAD:" + SHARED, ok=False)
        check(r.returncode != 0 and "REFUSED" in r.stderr, "hook refuses a gated push outside the queue", r)
        git(c, "reset", "-q", "--hard", "origin/" + SHARED)
        commit(c, "docs/note.md", "docs1", "c: docs only")
        r = git(c, "push", "origin", "HEAD:" + SHARED, ok=False)
        check(r.returncode == 0, "hook passes an ungated (docs-only) push", r)

        # -- the invariant: a scripts/ file a tier reaches is gated; one it does not reach is not
        r = pq(c, "C", "closure")
        check(r.stdout.split() == ["scripts/tier_helper.py"], "closure = the file the tier calls (comments ignored)", r)
        git(c, "pull", "-q", "--rebase", "origin", SHARED)
        commit(c, "scripts/tier_helper.py", "x = 2", "c: change a tier helper")
        r = git(c, "push", "origin", "HEAD:" + SHARED, ok=False)
        check(r.returncode != 0 and "tier_helper" in r.stderr, "hook refuses a change to a file the tier reaches", r)
        git(c, "reset", "-q", "--hard", "origin/" + SHARED)
        commit(c, "scripts/unrelated.py", "x = 2", "c: change an unrelated script")
        r = git(c, "push", "origin", "HEAD:" + SHARED, ok=False)
        check(r.returncode == 0, "hook passes a script no tier reaches", r)
        commit(c, "references/Deck/claude_s1_gi0.json", "{}", "c: a reference")
        r = git(c, "push", "origin", "HEAD:" + SHARED, ok=False)
        check(r.returncode == 0, "references are never delayed (the one exemption)", r)

        # -- join order, wait, land
        git(b, "pull", "-q", "--rebase", "origin", SHARED)
        commit(b, "src/a.cpp", "b1", "b: src change")
        commit(c, "test/regression_gt.txt", "c2", "c: gt change")
        r = pq(b, "B", "join")
        check(r.returncode == 0 and "HEAD" in r.stdout, "first joiner is the head", r)
        r = pq(c, "C", "join")
        check(r.returncode == 0 and "number 1" in r.stdout, "second joiner is number 1", r)
        r = pq(c, "C", "wait", "--poll-s", "0.2", "--max-wait-min", "0.01")
        check(r.returncode == 75, "wait exits 75 while not the head", r)
        r = pq(c, "C", "land")
        check(r.returncode == 2, "land refuses a session that is not the head", r)
        r = pq(b, "B", "land")
        check(r.returncode == 0 and "lease passed to C" in r.stdout, "head lands and passes the lease", r)
        check(tip(b, "HEAD") == git(b, "ls-remote", "origin", "refs/heads/" + SHARED).stdout.split()[0],
              "origin's shared branch is the head's HEAD")
        r = pq(c, "C", "wait", "--poll-s", "0.2")
        check(r.returncode == 0, "wait exits 0 at the head", r)

        # -- the new head's base lacks B's gated commit: land refuses, rebase + land succeeds
        r = pq(c, "C", "land")
        check(r.returncode == 4 and "GATED" in r.stdout, "land refuses when origin moved with gated commits", r)
        git(c, "fetch", "-q", "origin")
        git(c, "rebase", "-q", "origin/" + SHARED)
        r = pq(c, "C", "land")
        check(r.returncode == 0 and "queue empty" in r.stdout, "land after the rebase; queue empties", r)

        # -- ungated commits landing meanwhile are rebased over automatically
        git(b, "pull", "-q", "--rebase", "origin", SHARED)
        commit(b, "src/b.cpp", "b2", "b: second src change")
        r = pq(b, "B", "join")
        git(c, "pull", "-q", "--rebase", "origin", SHARED)
        commit(c, "docs/other.md", "docs2", "c: docs while B holds the lease")
        r2 = git(c, "push", "origin", "HEAD:" + SHARED, ok=False)
        check(r2.returncode == 0, "an ungated push goes straight through while someone holds the lease", r2)
        r = pq(b, "B", "land")
        check(r.returncode == 0 and "ungated commits only" in r.stdout, "land rebases over ungated commits", r)

        # -- stale head reaped; requeue; leave; hold
        r = pq(b, "B", "join")
        r = pq(c, "C", "join")
        time.sleep(1.2)
        r = pq(c, "C", "wait", "--poll-s", "0.2", env={"MDT_PUSH_QUEUE_TTL_MIN": "0"})
        check(r.returncode == 0 and "stale head B" in r.stdout, "a stale head is reaped and the next one wakes", r)
        r = pq(b, "B", "join")
        r = pq(c, "C", "requeue")
        st = pq(c, "C", "status").stdout
        check(r.returncode == 0 and st.index("  B  ") < st.index("  C  "), "requeue moves the head to the back", r)
        r = pq(b, "B", "hold", "--", "true")
        check(r.returncode == 0 and "heartbeat" in git(b, "log", "-1", "--format=%s", "origin/push-queue").stdout,
              "hold runs the command and heartbeats", r)
        r = pq(b, "B", "hold", "--", "false")
        check(r.returncode == 1, "hold returns the command's exit code", r)
        pq(b, "B", "leave"), pq(c, "C", "leave")
        r = pq(c, "C", "status")
        check("empty" in r.stdout, "leave empties the queue", r)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    # -- the real repo: dependencies we know the build and tiers have must be in the computed closure
    real = run(os.path.join(HERE, ".."), sys.executable, SCRIPT, "--id", "t", "closure").stdout.split()
    for known in ("scripts/ref_bench.py", "scripts/deck_registry.py", "scripts/build_pgo.sh", "scripts/engine_bin.py"):
        check(known in real, "real closure includes %s" % known)
    print("\n%s" % ("ALL PASS" if not FAILS else "%d FAILED" % len(FAILS)))
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
