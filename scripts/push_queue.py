#!/usr/bin/env python3
"""Push queue for the shared branch (CLAUDE.md, "PUSH TO THE SHARED BRANCH THROUGH THE PUSH QUEUE").

Several sessions, on more than one container, push to one shared branch. Every push that moves engine
play makes the next pusher rebase, rebuild and re-run smoke, and before this queue a session could
validate, find origin had moved, and validate again. The queue serialises it: ONE session at a time
holds the lease (the queue's head), origin cannot move under it, so its one validation is enough. Nobody
else integrates while waiting: they keep working on their existing base and rebase once, when their turn
comes (USER 2026-11-19: *"keep working and only push changes when the queue opens up"*).

The queue lives on origin's `push-queue` branch as one file, queue.json (an ordered list; entries[0] is
the head). Every write is a commit on top of the tip we read, pushed WITHOUT force: a concurrent writer
makes our push fail and we re-read and retry (a compare-and-swap, the artifact queue's claim pattern).
Written with git plumbing only: no working tree, index or branch is touched.

    push_queue.py join [--note TEXT]   enter the queue (no rebase, no build -- carry on working)
                      [--urgent]       a fix for something breaking other sessions: right behind the head
    push_queue.py wait                 block until you are the head (run it as a background waiter)
    push_queue.py hold -- CMD ...      run CMD (your validation) and keep the lease alive while it runs
    push_queue.py keepalive --pid N    keep the lease alive while an already-running process lives
    push_queue.py heartbeat            refresh the lease once
    push_queue.py land                 push HEAD to the shared branch AND pass the lease on, atomically
    push_queue.py requeue              go to the back (validation failed: fix it, then wait again)
    push_queue.py leave                drop out
    push_queue.py status               print the queue
    push_queue.py install-hook         set core.hooksPath=.githooks (join does it too)

A head that stops heartbeating for the lease TTL (default 45 min, counted from when it became head or its
last heartbeat) is STALE: any waiter removes it and the next session becomes head.

Pushes that touch nothing which can move play or the tiers (GATED below) skip the queue: they never make
anyone rebuild. The pre-push hook (.githooks/pre-push -> `pre-push` here) refuses any other push to the
shared branch that does not come from `land`.
"""
import argparse
import datetime
import json
import os
import re
import socket
import subprocess
import sys
import time

SHARED = os.environ.get("MDT_SHARED_BRANCH", "phase-1-2-deck-analyzer")
QBRANCH = os.environ.get("MDT_PUSH_QUEUE_BRANCH", "push-queue")
REMOTE = "origin"
TTL_MIN = float(os.environ.get("MDT_PUSH_QUEUE_TTL_MIN", "45"))
HEARTBEAT_S = 600
ZERO = "0" * 40
LAND_ENV = "MDT_PUSH_QUEUE_LAND"
# THE INVARIANT (USER 2026-11-19: "changes that are outside of the important folders can skip the queue.
# We do need to be careful that this invariant holds going forward ... if it doesn't it needs to be flagged
# and addressed"): a push that touches no GATED path cannot change what the build produces or what a tier
# reports, so nobody has to rebuild or re-run smoke for it. GATED = the folders below PLUS every file the
# build and tier entry points can reach (shell calls, Python imports, JS requires), computed from the
# commit being pushed -- so a tier that starts reading a new scripts/ file gates that file the moment
# the change lands, with no list to keep up to date. `push_queue.py closure` prints the reachable set;
# test/push_queue_test.py fails if a reachable file is outside the folders and not reported.
GATED_PREFIXES = ("src/", "test/", "decks/", "cmake/")
GATED_FILES = ("CMakeLists.txt", "CMakePresets.json", "build.sh", "build.cmd", "build.ps1")
ENTRY_POINTS = ("build.sh", "CMakeLists.txt", "test/regression.sh", "test/scenarios.sh")
ENTRY_DIRS = ("test/lib/",)
# The ONE deliberate exception. The tiers' reference stages replay references/, but a new user reference
# is never delayed (CLAUDE.md play-server hard rules: commit AND push it at once). A reference that lands
# while a holder validates is not in that validation; the next tier run replays it, and a reference that
# stops replaying is the agent's to fix (CLAUDE.md, references are COMMIT-ONLY).
EXEMPT_PREFIXES = ("references/",)


def gated(path, closure=frozenset()):
    if path.startswith(EXEMPT_PREFIXES):
        return False
    return path.startswith(GATED_PREFIXES) or path in GATED_FILES or path in closure


_REF_RE = re.compile(r"\b((?:scripts|tools|test)/[A-Za-z0-9_./-]+\.(?:py|sh|js|mjs))\b")
_HERE_RE = re.compile(r"\$\{?(?:HERE|DIR|SCRIPT_DIR|ROOT|REPO)\}?/([A-Za-z0-9_./-]+\.(?:py|sh|js))")
_PY_IMPORT_RE = re.compile(r"^\s*(?:from\s+([A-Za-z_][\w]*)[\w.]*\s+import|import\s+([A-Za-z_][\w, ]*))", re.M)
_JS_REQ_RE = re.compile(r"""require\(\s*['"](\.{1,2}/[^'"]+)['"]\s*\)|from\s+['"](\.{1,2}/[^'"]+)['"]""")


def tier_closure(rev="HEAD"):
    """Every file under scripts/ or tools/ reachable from the build and tier entry points at `rev`.
    Conservative: a path named anywhere in a reachable file (a comment included) counts as reached."""
    files = set(git("ls-tree", "-r", "--name-only", rev).splitlines())
    todo = [f for f in files if f in ENTRY_POINTS or f.startswith(ENTRY_DIRS)]
    seen, out = set(todo), set()

    def add(p):
        p = os.path.normpath(p)
        if p in files and p not in seen:
            seen.add(p)
            todo.append(p)
            if not gated(p):
                out.add(p)
    while todo:
        f = todo.pop()
        r = git("cat-file", "-p", "%s:%s" % (rev, f), check=False)
        if r.returncode != 0:
            continue
        # Comment LINES do not call anything; a path in code (a string included) does.
        text = "\n".join(l for l in r.stdout.splitlines() if not l.lstrip().startswith(("#", "//")))
        d = os.path.dirname(f)
        for m in _REF_RE.finditer(text):
            add(m.group(1))
        for m in _HERE_RE.finditer(text):
            add(os.path.join(d, m.group(1)))
            add(m.group(1))
        if f.endswith(".py"):
            for m in _PY_IMPORT_RE.finditer(text):
                names = [m.group(1)] if m.group(1) else [n.strip().split()[0] for n in m.group(2).split(",") if n.strip()]
                for n in names:
                    for base in (d, "scripts", "test"):
                        add(os.path.join(base, n + ".py"))
        if f.endswith((".js", ".mjs")):
            for m in _JS_REQ_RE.finditer(text):
                p = os.path.join(d, m.group(1) or m.group(2))
                add(p if p.endswith((".js", ".mjs", ".json")) else p + ".js")
    return out


def git(*args, inp=None, check=True, env=None):
    r = subprocess.run(["git"] + list(args), input=inp, capture_output=True, text=True,
                       env=dict(os.environ, **(env or {})))
    if check and r.returncode != 0:
        raise RuntimeError("git %s failed: %s" % (" ".join(args), r.stderr.strip()))
    return r.stdout if check else r


def utcnow():
    return datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0)


def iso(t):
    return t.strftime("%Y-%m-%dT%H:%M:%SZ")


def parse_iso(s):
    return datetime.datetime.strptime(s, "%Y-%m-%dT%H:%M:%SZ").replace(tzinfo=datetime.timezone.utc)


def log(msg):
    print("[push-queue %s] %s" % (utcnow().strftime("%H:%M:%S"), msg), flush=True)


def current_branch():
    return git("rev-parse", "--abbrev-ref", "HEAD").strip()


def my_id(a):
    return a.id or os.environ.get("MDT_PUSH_QUEUE_ID") or "%s:%s" % (socket.gethostname(), current_branch())


# ---------------------------------------------------------------------------------------------- queue
def q_fetch():
    git("fetch", "-q", REMOTE, "+refs/heads/%s:refs/remotes/%s/%s" % (QBRANCH, REMOTE, QBRANCH), check=False)


def q_tip():
    r = git("rev-parse", "-q", "--verify", "refs/remotes/%s/%s" % (REMOTE, QBRANCH), check=False)
    return r.stdout.strip() or None


def q_read(tip):
    if not tip:
        return {"entries": []}
    return json.loads(git("show", "%s:queue.json" % tip))


def q_remote_tip():
    out = git("ls-remote", REMOTE, "refs/heads/%s" % QBRANCH, check=False).stdout.split()
    return out[0] if out else None


def q_commit(parent, data, msg):
    blob = git("hash-object", "-w", "--stdin", inp=json.dumps(data, indent=1, sort_keys=True) + "\n").strip()
    tree = git("mktree", inp="100644 blob %s\tqueue.json\n" % blob).strip()
    args = ["commit-tree", tree, "-m", "push-queue: " + msg]
    if parent:
        args[2:2] = ["-p", parent]
    return git(*args).strip()


def q_cas(mutate, what):
    """Re-read the queue, apply mutate(data) -> (changed, result), push without force; retry on a race."""
    for _ in range(10):
        q_fetch()
        t = q_tip()
        data = q_read(t)
        changed, result = mutate(data)
        if not changed:
            return result
        c = q_commit(t, data, what)
        r = git("push", "-q", REMOTE, "%s:refs/heads/%s" % (c, QBRANCH), check=False)
        if r.returncode == 0:
            git("update-ref", "refs/remotes/%s/%s" % (REMOTE, QBRANCH), c)
            return result
        time.sleep(3)
    raise RuntimeError("could not update %s after 10 attempts" % QBRANCH)


def find(data, ident):
    for i, e in enumerate(data["entries"]):
        if e["id"] == ident:
            return i
    return -1


def promote_head(data):
    if data["entries"]:
        h = data["entries"][0]
        if not h.get("head_since"):
            h["head_since"] = iso(utcnow())
            h.pop("heartbeat", None)


def remove(data, i):
    data["entries"].pop(i)
    promote_head(data)


def last_alive(e):
    t = parse_iso(e["head_since"])
    if e.get("heartbeat"):
        t = max(t, parse_iso(e["heartbeat"]))
    return t


def is_stale(e):
    return e.get("head_since") and (utcnow() - last_alive(e)).total_seconds() > TTL_MIN * 60


def reap_stale_head():
    """Remove a stale head. -> the id removed, or None."""
    def m(data):
        if data["entries"] and is_stale(data["entries"][0]):
            gone = data["entries"][0]["id"]
            remove(data, 0)
            return True, gone
        return False, None
    gone = q_cas(m, "reaped a stale head (no heartbeat for %g min)" % TTL_MIN)
    if gone:
        log("removed the stale head %s (no heartbeat for %g min)" % (gone, TTL_MIN))
    return gone


# ------------------------------------------------------------------------------------------- commands
def cmd_install_hook(a):
    cur = git("config", "--get", "core.hooksPath", check=False).stdout.strip()
    if cur == ".githooks":
        return 0
    if cur:
        log("core.hooksPath is already %r -- not overwriting it; the push-queue hook is NOT active here" % cur)
        return 1
    git("config", "core.hooksPath", ".githooks")
    log("set core.hooksPath=.githooks (the push-queue pre-push hook is active in this clone)")
    return 0


def cmd_status(a):
    q_fetch()
    data = q_read(q_tip())
    ident = my_id(a)
    if not data["entries"]:
        print("push queue is empty: the next gated push can `join` and land at once")
        return 0
    for i, e in enumerate(data["entries"]):
        tag = "HEAD" if i == 0 else "%4d" % i
        extra = ""
        if i == 0:
            extra = " (head %s, last alive %s%s)" % (e["head_since"], iso(last_alive(e)),
                                                    ", STALE" if is_stale(e) else "")
        print("%s  %s  branch=%s joined=%s%s%s%s" % (tag, e["id"], e.get("branch", "?"), e["joined"], extra,
                                                   ("  -- " + e["note"]) if e.get("note") else "",
                                                   "   <- you" if e["id"] == ident else ""))
    return 0


def cmd_join(a):
    cmd_install_hook(a)
    ident = my_id(a)

    def m(data):
        i = find(data, ident)
        if i >= 0:
            return False, i
        e = {"id": ident, "branch": current_branch(), "host": socket.gethostname(),
             "joined": iso(utcnow()), "note": a.note or ""}
        if a.urgent and data["entries"]:
            # A fix for something breaking OTHER sessions goes right behind the current head -- never in
            # front of it: the head may be mid-validation, and moving origin under it is what the queue
            # exists to prevent.
            e["urgent"] = True
            data["entries"].insert(1, e)
            promote_head(data)
            return True, 1
        data["entries"].append(e)
        promote_head(data)
        return True, len(data["entries"]) - 1
    pos = q_cas(m, "join %s" % ident)
    if pos == 0:
        log("%s is at the HEAD: rebase, validate, `land`" % ident)
    else:
        log("%s is number %d in the queue: keep working; `wait` wakes you when it is your turn" % (ident, pos))
    return 0


def cmd_leave(a):
    ident = my_id(a)

    def m(data):
        i = find(data, ident)
        if i < 0:
            return False, False
        remove(data, i)
        return True, True
    log(("%s left the queue" if q_cas(m, "leave %s" % ident) else "%s was not in the queue") % ident)
    return 0


def cmd_requeue(a):
    ident = my_id(a)

    def m(data):
        i = find(data, ident)
        if i < 0:
            return False, False
        e = data["entries"].pop(i)
        for k in ("head_since", "heartbeat"):
            e.pop(k, None)
        e["joined"] = iso(utcnow())
        data["entries"].append(e)
        promote_head(data)
        return True, True
    if not q_cas(m, "requeue %s" % ident):
        log("%s is not in the queue (use join)" % ident)
        return 3
    log("%s moved to the back of the queue" % ident)
    return 0


def heartbeat_once(ident):
    def m(data):
        if not data["entries"] or data["entries"][0]["id"] != ident:
            return False, False
        data["entries"][0]["heartbeat"] = iso(utcnow())
        return True, True
    return q_cas(m, "heartbeat %s" % ident)


def cmd_heartbeat(a):
    if heartbeat_once(my_id(a)):
        return 0
    log("%s is not the head: nothing to refresh" % my_id(a))
    return 2


def keep_alive(ident, alive):
    last = 0.0
    while alive():
        if time.time() - last >= HEARTBEAT_S:
            try:
                if not heartbeat_once(ident):
                    log("WARNING: %s is no longer the head (reaped as stale?) -- `join` again before landing" % ident)
            except Exception as ex:            # a network blip must not kill the validation it is guarding
                log("heartbeat failed (will retry): %s" % ex)
            last = time.time()
        time.sleep(15)


def cmd_hold(a):
    if not a.cmd:
        log("hold needs a command: push_queue.py hold -- <command ...>")
        return 2
    cmd = a.cmd[1:] if a.cmd[0] == "--" else a.cmd
    p = subprocess.Popen(cmd)
    keep_alive(my_id(a), lambda: p.poll() is None)
    heartbeat_once(my_id(a))
    return p.returncode


def cmd_keepalive(a):
    def alive():
        try:
            os.kill(a.pid, 0)
            return True
        except OSError:
            return False
    keep_alive(my_id(a), alive)
    heartbeat_once(my_id(a))
    return 0


def cmd_wait(a):
    """Exit 0 once we are the head; 3 if we are not in the queue; 75 after --max-wait-min (re-arm it)."""
    ident = my_id(a)
    deadline = time.time() + a.max_wait_min * 60
    seen = object()
    last_read = 0.0
    while True:
        rt = q_remote_tip()
        if rt != seen or time.time() - last_read > 300:
            seen, last_read = rt, time.time()
            q_fetch()
            data = q_read(q_tip())
            i = find(data, ident)
            if i < 0:
                log("%s is not in the queue (join first)" % ident)
                return 3
            if i == 0:
                log("%s is at the HEAD: rebase onto origin/%s, validate, then `land`" % (ident, SHARED))
                return 0
            if is_stale(data["entries"][0]):
                reap_stale_head()
                seen = object()
                continue
        if time.time() > deadline:
            log("still waiting after %g min (re-arm `wait`)" % a.max_wait_min)
            return 75
        time.sleep(a.poll_s)


def commits_files(rng):
    return [f for f in git("log", "--name-only", "--format=", rng).splitlines() if f]


def cmd_land(a):
    ident = my_id(a)
    for _ in range(10):
        git("fetch", "-q", REMOTE, "+refs/heads/%s:refs/remotes/%s/%s" % (SHARED, REMOTE, SHARED))
        base = git("rev-parse", "refs/remotes/%s/%s" % (REMOTE, SHARED)).strip()
        q_fetch()
        t = q_tip()
        data = q_read(t)
        if not data["entries"] or data["entries"][0]["id"] != ident:
            i = find(data, ident)
            log("%s is not the head (%s): join and wait for your turn" % (
                ident, "not in the queue" if i < 0 else "position %d" % i))
            return 2
        if git("merge-base", "--is-ancestor", base, "HEAD", check=False).returncode != 0:
            cl = tier_closure(base)
            moved = sorted(set(f for f in commits_files("HEAD..%s" % base) if gated(f, cl)))
            if moved:
                log("origin/%s moved with GATED changes since your rebase (someone bypassed the queue?): %s -- "
                    "rebase, re-validate, then land" % (SHARED, ", ".join(moved[:8])))
                return 4
            if a.no_rebase:
                log("origin/%s moved (ungated changes only); rebase onto it and land again" % SHARED)
                return 4
            r = git("rebase", "-q", base, check=False)
            if r.returncode != 0:
                git("rebase", "--abort", check=False)
                log("rebase onto the new ungated commits conflicted (aborted): resolve it by hand, then land; "
                    "the incoming commits touch no gated path, so no re-validation is needed")
                return 5
            log("rebased onto %s (ungated commits only: no re-validation needed)" % base[:9])
        if git("rev-parse", "HEAD").strip() == base:
            log("nothing to push: HEAD is origin/%s" % SHARED)
        nxt = json.loads(json.dumps(data))
        remove(nxt, 0)
        c = q_commit(t, nxt, "land %s -> %s" % (ident, nxt["entries"][0]["id"] if nxt["entries"] else "(empty)"))
        refs = ["HEAD:refs/heads/%s" % SHARED, "%s:refs/heads/%s" % (c, QBRANCH)]
        r = git("push", "--atomic", REMOTE, *refs, check=False, env={LAND_ENV: "1"})
        if r.returncode != 0 and "atomic" in r.stderr and "support" in r.stderr:
            # A server without atomic pushes: shared branch first (it is what matters), then the dequeue.
            r = git("push", REMOTE, refs[0], check=False, env={LAND_ENV: "1"})
            if r.returncode == 0:
                cmd_leave(a)
        elif r.returncode == 0:
            git("update-ref", "refs/remotes/%s/%s" % (REMOTE, QBRANCH), c)
        if r.returncode == 0:
            sha = git("rev-parse", "HEAD").strip()
            git("update-ref", "refs/remotes/%s/%s" % (REMOTE, SHARED), sha)
            log("LANDED %s on %s; the lease passed to %s" % (
                sha[:9], SHARED, nxt["entries"][0]["id"] if nxt["entries"] else "nobody (queue empty)"))
            return 0
        log("push lost a race (%s); re-reading" % (r.stderr.strip().splitlines() or ["?"])[-1])
        time.sleep(3)
    log("could not land after 10 attempts")
    return 6


def cmd_pre_push(a):
    """The git pre-push hook. stdin: `<local ref> <local sha> <remote ref> <remote sha>` per ref."""
    if os.environ.get(LAND_ENV) == "1":
        return 0
    bad = []
    for line in sys.stdin.read().splitlines():
        parts = line.split()
        if len(parts) != 4 or parts[2] != "refs/heads/%s" % SHARED:
            continue
        lsha, rsha = parts[1], parts[3]
        if lsha == ZERO or rsha == ZERO:
            bad.append("deleting or creating %s" % SHARED)
            continue
        r = git("log", "--name-only", "--format=", "%s..%s" % (rsha, lsha), check=False)
        if r.returncode != 0:
            bad.append("origin's tip %s is not in this clone (fetch and rebase first)" % rsha[:9])
            continue
        cl = tier_closure(lsha)
        bad += sorted(set(f for f in r.stdout.splitlines() if f and gated(f, cl)))
    if not bad:
        return 0
    sys.stderr.write(
        "\npush REFUSED by the push queue (CLAUDE.md, 'PUSH TO THE SHARED BRANCH THROUGH THE PUSH QUEUE'):\n"
        "  this push to %s touches gated paths (%s%s)\n"
        "  go through the queue:  python3 scripts/push_queue.py join   (then keep working)\n"
        "                         python3 scripts/push_queue.py wait   (background waiter; exits at your turn)\n"
        "                         rebase, validate, then  python3 scripts/push_queue.py land\n"
        "  Pushes touching none of %s (nor a scripts/ or tools/ file the build or a tier reaches:\n"
        "  `push_queue.py closure`) go straight through.\n\n"
        % (SHARED, ", ".join(bad[:6]), ", ..." if len(bad) > 6 else "",
           " ".join(GATED_PREFIXES + GATED_FILES)))
    return 1


def cmd_closure(a):
    for f in sorted(tier_closure(a.rev)):
        print(f)
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--id", help="queue identity (default $MDT_PUSH_QUEUE_ID, else <hostname>:<branch>)")
    sub = ap.add_subparsers(dest="cmd_name", required=True)
    sub.add_parser("status")
    sub.add_parser("install-hook")
    p = sub.add_parser("join")
    p.add_argument("--note", default="")
    p.add_argument("--urgent", action="store_true",
                   help="a fix for something breaking other sessions (red CI, a broken build or tier): "
                        "queue right behind the current head")
    sub.add_parser("leave")
    sub.add_parser("requeue")
    sub.add_parser("heartbeat")
    p = sub.add_parser("wait")
    p.add_argument("--poll-s", type=float, default=120)
    p.add_argument("--max-wait-min", type=float, default=110,
                   help="exit 75 after this long so a tool-hosted waiter can be re-armed (default 110)")
    p = sub.add_parser("hold")
    p.add_argument("cmd", nargs=argparse.REMAINDER)
    p = sub.add_parser("keepalive")
    p.add_argument("--pid", type=int, required=True)
    p = sub.add_parser("land")
    p.add_argument("--no-rebase", action="store_true",
                   help="do not rebase onto ungated commits that landed meanwhile; exit 4 instead")
    p = sub.add_parser("closure", help="print the scripts/ and tools/ files the build and tiers reach (gated)")
    p.add_argument("--rev", default="HEAD")
    p = sub.add_parser("pre-push")
    p.add_argument("hook_args", nargs="*")
    a = ap.parse_args()
    fn = {"status": cmd_status, "install-hook": cmd_install_hook, "join": cmd_join, "leave": cmd_leave,
          "requeue": cmd_requeue, "heartbeat": cmd_heartbeat, "wait": cmd_wait, "hold": cmd_hold,
          "keepalive": cmd_keepalive, "land": cmd_land, "pre-push": cmd_pre_push, "closure": cmd_closure}[a.cmd_name]
    return fn(a)


if __name__ == "__main__":
    sys.exit(main())
