#!/usr/bin/env python3
"""Resume-path test for scripts/artifact_queue.py's worker (USER 2026-11-05: "we should fix anything not making
it effectively resumable"). No engine, no games: a scratch git repo stands in for the worker's frozen worktree,
and stub build.sh / valueleaf.sh / mullgen.sh scripts exit non-zero or are killed.

Asserts that a verdictless driver exit (crash, OOM-kill, SIGTERM, build failure mid-job) KEEPS the job in flight
-- inflight.json, the worktree (no result branch, no re-sync) and logs/vlq_<deck> -- that a resume carries the
claim's first `started` over, that the ledger and result commit record the worktree's real HEAD / HEAD:src, that
the retry cap stops the worker without retiring the job, and that `abandon` is what retires it.

    python3 test/artifact_queue_resume_test.py
"""
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile
import types

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("aq", os.path.join(HERE, "..", "scripts", "artifact_queue.py"))
AQ = importlib.util.module_from_spec(spec)
spec.loader.exec_module(AQ)

OK = [0, 0]


def check(name, cond):
    OK[0 if cond else 1] += 1
    print("  %s  %s" % ("ok  " if cond else "FAIL", name))


def sh(cwd, *args):
    return subprocess.run(list(args), cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


class FakeQueue(object):
    """In-memory stand-in for the artifact-queue branch: honours put_claim's CAS precondition."""
    claims_ = {}

    def __init__(self, *a, **k):
        pass

    def fetch(self):
        pass

    def claims(self):
        return dict(FakeQueue.claims_)

    def _write(self, deck, claim, expect=None):
        if expect is not None and not expect(FakeQueue.claims_.get(deck)):
            return False
        FakeQueue.claims_[deck] = json.loads(json.dumps(claim))
        return True


STUB = r"""#!/bin/bash
# behaviour comes from $STUBCTL/<name>: one line, "rc=<n>" | "kill" | "complete" (valueleaf) | "pass" (mullgen)
name=$(basename "$0" .sh)
mode=$(cat "$STUBCTL/$name" 2>/dev/null || echo rc=0)
echo "stub $name mode=$mode args=$*"
if [ "$name" = valueleaf ]; then
  mkdir -p logs/vlq_d/done
  [ -e logs/vlq_d/done/phaseA ] && echo "RESUMED: phaseA marker present" >> "$STUBCTL/vl_seen"
  touch logs/vlq_d/done/phaseA
fi
case "$mode" in
  kill) kill -KILL $$ ;;
  complete) echo "=== COMPLETE"; exit 0 ;;
  rc=*) exit "${mode#rc=}" ;;
esac
"""


def main():
    T = tempfile.mkdtemp(prefix="aq_resume_test_")
    env_keys = ("GIT_AUTHOR_NAME", "GIT_COMMITTER_NAME", "GIT_AUTHOR_EMAIL", "GIT_COMMITTER_EMAIL", "STUBCTL")
    saved_env = {k: os.environ.get(k) for k in env_keys}
    os.environ.update({"GIT_AUTHOR_NAME": "t", "GIT_COMMITTER_NAME": "t", "GIT_AUTHOR_EMAIL": "t@localhost",
                       "GIT_COMMITTER_EMAIL": "t@localhost", "STUBCTL": os.path.join(T, "ctl")})
    os.makedirs(os.path.join(T, "ctl"))
    W, SD = os.path.join(T, "wt"), os.path.join(T, "state")
    os.makedirs(SD)
    try:
        # ---- the scratch "frozen worktree": frozen commit, then a cherry-picked src change on top (HEAD != frozen)
        os.makedirs(os.path.join(W, "src"))
        os.makedirs(os.path.join(W, "scripts"))
        os.makedirs(os.path.join(W, "decks", "D"))
        open(os.path.join(W, "src", "a.cpp"), "w").write("int a;\n")
        open(os.path.join(W, "decks", "D", "D.txt"), "w").write("4 Forest\n")
        for n in ("build", "scripts/valueleaf", "scripts/mullgen"):
            p = os.path.join(W, n + ".sh")
            open(p, "w").write(STUB)
            os.chmod(p, 0o755)
        open(os.path.join(W, ".gitignore"), "w").write("logs/\n")
        sh(W, "git", "init", "-q")
        sh(W, "git", "add", "-A")
        sh(W, "git", "commit", "-q", "-m", "frozen")
        frozen = sh(W, "git", "rev-parse", "HEAD")
        open(os.path.join(W, "src", "a.cpp"), "a").write("int b;\n")
        sh(W, "git", "commit", "-q", "-am", "cherry-picked fix")
        head, head_src = sh(W, "git", "rev-parse", "HEAD"), sh(W, "git", "rev-parse", "HEAD:src")

        ctl = lambda name, mode: open(os.path.join(T, "ctl", name), "w").write(mode)
        synced = []
        regzipped = []
        AQ.state_dir = lambda wid: SD
        AQ.worktree_path = lambda wid: W
        AQ.wait_idle = lambda wid, cfg: False
        AQ.busy_reason = lambda cfg: None
        AQ.Queue = FakeQueue
        AQ.worker_id = lambda: "W1"
        AQ.refuse_temp_root = lambda: None
        AQ.sync_worktree = lambda wid, cfg: synced.append(1) or (W, head)
        real_regzip = AQ.regzip
        AQ.regzip = lambda wt, d: regzipped.append(1) or real_regzip(wt, d)
        deckinfo = {"dir": "decks/D", "stem": "D", "value": "decks/D/D.value.json", "profile": "decks/D/D.profile.json"}
        AQ.shipping_decks = lambda root=None: [types.SimpleNamespace(key="d", **deckinfo)]
        cfg = dict(AQ.DEFAULT_CONFIG, auto_land=False, max_attempts=2, retry_backoff_minutes=0)
        AQ.load_config = lambda root=None: cfg
        ifp = os.path.join(SD, "inflight.json")
        q = FakeQueue()
        job = {"deck": "d", "stages": ["valueleaf"], "blobs": {"valueleaf": "em:old"}, "score": 1.0,
               "queued_kinds": ["valueleaf"]}
        branches = lambda: sh(W, "git", "branch", "--list", "artifact-refresh/*")

        print("a driver killed mid-run (SIGKILL) keeps the job in flight:")
        ctl("valueleaf", "kill")
        out = AQ.run_job("W1", cfg, q, job, W, frozen, deckinfo, False)
        st = json.load(open(ifp)) if os.path.exists(ifp) else {}
        check("outcome is retry, not failed", out == "retry")
        check("inflight.json kept", bool(st))
        check("attempt recorded with rc and log path",
              st.get("attempts") and st["attempts"][-1]["rc"] == -9 and st["attempts"][-1]["log"].endswith("d.valueleaf.log"))
        check("claim is `paused` (no other worker takes it)", FakeQueue.claims_["d"]["state"] == "paused")
        check("vl_queue_fresh pinned, so the resume does not move the queue dir aside", st.get("vl_queue_fresh") is True)
        check("no result branch was committed", branches() == "")
        check("worktree HEAD untouched", sh(W, "git", "rev-parse", "HEAD") == head)
        check("claim records the worktree HEAD", st["claim"].get("head") == head)

        print("resume after the crash (a USER pause leaves the same state):")
        # The Hinata2 shape: an older worker reset `started` on resume; the run_id stamp holds the first claim.
        st["claim"]["started"] = "2026-11-04T15:46:11Z"
        st["claim"]["run_id"] = "d-W1-20261025T010853"
        json.dump(st, open(ifp, "w"))
        FakeQueue.claims_["d"] = dict(st["claim"])
        ctl("valueleaf", "complete")
        out = AQ.run_job("W1", cfg, q, job, W, frozen, deckinfo, False)
        check("the resumed run completes", out == "done")
        seen = open(os.path.join(T, "ctl", "vl_seen")).read() if os.path.exists(os.path.join(T, "ctl", "vl_seen")) else ""
        check("valueleaf.sh found its queue dir's markers (not moved aside)", "RESUMED" in seen)
        check("no queue dir was moved aside", not [x for x in os.listdir(os.path.join(W, "logs")) if ".prev-" in x])
        c = FakeQueue.claims_["d"]
        check("started carried over to the FIRST claim (run_id stamp)", c.get("started") == "2026-10-25T01:08:53Z")
        check("run_id carried over", c.get("run_id") == "d-W1-20261025T010853")
        check("inflight.json retired only on the verdict", not os.path.exists(ifp))
        led = AQ.read_ledger(W, "d")
        e = led[-1] if led else {}
        check("ledger records worktree HEAD and HEAD:src", e.get("head") == head and e.get("src_tree") == head_src)
        check("ledger names the frozen src tree and warns", e.get("frozen_src_tree") == sh(W, "git", "rev-parse", frozen + ":src")
              and "DIFFERS" in (e.get("provenance_warning") or ""))
        br = branches().strip(" *")
        msg = sh(W, "git", "log", "-1", "--format=%B", br) if br else ""
        check("result commit names HEAD and warns it differs from frozen", head[:9] in msg and "DIFFERS" in msg)
        sh(W, "git", "checkout", "-q", "--detach", head)

        print("versus_cost reads rounds since the carried-over start:")
        vd = os.path.join(W, "logs", "keepmodel_exh_versus_D_r1")
        os.makedirs(vd)
        open(os.path.join(vd, "batch.log"), "w").write("s1@old: played=1 x units=100\ns1@new: played=1 x units=110\n"
                                                       "s2@old: played=1 x units=100\ns2@new: played=1 x units=112\n")
        t = AQ.parse_iso("2026-10-26T00:00:00Z").timestamp()
        os.utime(os.path.join(vd, "batch.log"), (t, t))
        check("a pre-pause round counts from the first start", AQ.versus_cost(W, "D", "2026-10-25T01:08:53Z") is not None)
        check("...and was dropped from a reset start (the old defect)", AQ.versus_cost(W, "D", "2026-11-04T15:46:11Z") is None)

        print("build failure on a RESUMED job holds it in flight; on a fresh job it releases:")
        FakeQueue.claims_.pop("d")              # the verdict above is waiting to land; start a new job
        ctl("valueleaf", "kill")
        check("(setup) a fresh job crashes into flight", AQ.run_job("W1", cfg, q, job, W, head, deckinfo, False) == "retry")
        ctl("build", "rc=1")
        try:
            AQ.run_job("W1", cfg, q, job, W, head, deckinfo, False)
            raised = False
        except AQ.MachineFault:
            raised = True
        check("MachineFault raised (worker stops)", raised)
        check("...inflight.json kept for the resumed job", os.path.exists(ifp))
        check("...claim paused, not released", FakeQueue.claims_["d"]["state"] == "paused")
        os.rename(ifp, ifp + ".hold")
        FakeQueue.claims_.pop("d")
        try:
            AQ.run_job("W1", cfg, q, job, W, head, deckinfo, False)
        except AQ.MachineFault:
            pass
        check("a fresh job that ran nothing is released", FakeQueue.claims_["d"]["state"] == "released"
              and not os.path.exists(ifp))
        os.rename(ifp + ".hold", ifp)
        ctl("build", "rc=0")

        print("a mulligan driver killed without a verdict is not regzipped and stays in flight:")
        mjob = dict(job, deck="d", stages=["mulligan"], first_time={"mulligan": True}, queued_kinds=["mulligan"])
        os.rename(ifp, ifp + ".hold")
        FakeQueue.claims_["d"] = dict(FakeQueue.claims_["d"], state="paused", worker="W1")
        AQ.screened_setting = lambda wt, d: (None, None)
        ctl("mullgen", "rc=143")
        del regzipped[:]
        out = AQ.run_job("W1", cfg, q, mjob, W, head, deckinfo, False)
        st = json.load(open(ifp)) if os.path.exists(ifp) else {}
        check("outcome retry", out == "retry")
        check("regzip not run on a verdictless exit", not regzipped)
        check("attempt rc 143 recorded", st.get("attempts") and st["attempts"][-1]["rc"] == 143)
        os.remove(ifp)
        os.rename(ifp + ".hold", ifp)

        print("cmd_run: resumes in place and stops at the retry cap without retiring the job:")
        del synced[:]
        n0 = len(json.load(open(ifp)).get("attempts", []))
        ctl("valueleaf", "rc=1")
        rc = AQ.cmd_run(types.SimpleNamespace(dry_run=False, once=False))
        st = json.load(open(ifp)) if os.path.exists(ifp) else {}
        check("worker exits 1 at the cap", rc == 1)
        check("never re-synced the worktree", not synced)
        check("two more attempts recorded", len(st.get("attempts", [])) == n0 + 2)
        check("job still in flight, claim paused", bool(st) and FakeQueue.claims_["d"]["state"] == "paused")

        print("abandon is what retires it:")
        rc = AQ.cmd_abandon(types.SimpleNamespace(why="test"))
        check("abandon renames inflight.json aside", rc == 0 and not os.path.exists(ifp)
              and [x for x in os.listdir(SD) if x.startswith("inflight.abandoned-")])
        check("claim failed (crash retry window)", FakeQueue.claims_["d"]["state"] == "failed")
    finally:
        shutil.rmtree(T, ignore_errors=True)
        for k, v in saved_env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
    print("%d passed, %d FAILED" % tuple(OK))
    return 1 if OK[1] else 0


if __name__ == "__main__":
    sys.exit(main())
