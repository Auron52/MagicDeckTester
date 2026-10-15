#!/usr/bin/env python3
"""Stage an `mtg --batch` run onto the build VOLUME so a repo-mount (9p) failure cannot kill it.

WHY. The repo is a 9p mount of the Windows host folder. It has died outright twice (2026-10-07,
2026-10-15; EIO on every path until WSL restarts). A batch whose binary lives on the mount dies of
SIGBUS when its text pages cannot be re-read, a job that starts after the drop cannot open its deck,
and output written to logs/ is lost. The build/ directory is a Docker volume (ext4) and survives.

WHAT. Copies everything a batch reads at RUNTIME into <volume>/runs/<name>/, keeping the manifest's
relative paths unchanged, then (with --launch) starts it detached from there:
  * the binary                      -> bin/mtg
  * src/cards/data/cards.json       (read CWD-relative at startup)
  * src/ai/data/heuristic_defaults.env (found by walking UP from the binary: without a copy in the
    run dir the walk would reach the PRIMARY checkout's file, i.e. silently the wrong tree;
    written empty when the tree has none, which is byte-identical to absent)
  * every file referenced by a job (deck, profile, ...) together with its directory's top-level
    files, symlinks dereferenced (sibling models resolve directory-relative off the profile)
Outputs (batch.out, batch.err, --game-log-dir, logs/batch/heartbeat) land in the run dir too.

  python3 scripts/vol_stage.py --root <tree> --manifest <m.json> --name <run> \
      [--bin build/PGO/mtg] [--env K=V ...] [--game-log-dir wins] [--publish logs/<dir>] [--launch]
  python3 scripts/vol_stage.py --publish-existing <run> --root <tree> --publish logs/<dir>

PUBLISHING (making a run visible on the Windows side). The run dir lives on the volume, which the host
cannot see. --publish DIR (relative to --root) copies the run's OUTPUTS -- batch.out, batch.err,
STAGED.json, manifest.json, run.sh and the --game-log-dir -- into the repo when the batch ends; a failed
copy (mount down) is reported and the volume copy is kept. --publish-existing does the same for a run
that already finished. Batch runs are recreatable logs; GENERATION artifacts do not go through this tool
(they stay on the repo drive, see docs/design/repo-mount-failures.md).
"""
import argparse, hashlib, json, os, shutil, subprocess, sys, time

VOLUME = "/workspaces/MagicDeckTester2/build/runs"


def fstype(path):
    p = path
    while not os.path.exists(p):
        p = os.path.dirname(p)
    out = subprocess.run(["findmnt", "-n", "-o", "FSTYPE", "-T", p], capture_output=True, text=True)
    return out.stdout.strip()


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def git(root, *a):
    r = subprocess.run(["git", "-C", root, *a], capture_output=True, text=True)
    return r.stdout.strip() if r.returncode == 0 else ""


OUTPUTS = ("batch.out", "batch.err", "STAGED.json", "manifest.json", "run.sh")


def publish(run, dest, game_log_dir=None):
    """Copy a run's outputs to `dest` (on the repo drive). Returns 0 on success, 1 when the copy failed."""
    items = [os.path.join(run, f) for f in OUTPUTS if os.path.exists(os.path.join(run, f))]
    if game_log_dir and os.path.isdir(os.path.join(run, game_log_dir)):
        items.append(os.path.join(run, game_log_dir))
    try:
        os.makedirs(dest, exist_ok=True)
        rc = subprocess.run(["rsync", "-a", *items, dest + "/"]).returncode
    except OSError as e:
        print(f"vol_stage: publish to {dest} FAILED ({e}); the run is intact at {run}")
        return 1
    print(f"vol_stage: published {len(items)} item(s) from {run} -> {dest}" if rc == 0 else
          f"vol_stage: publish to {dest} FAILED (rsync rc={rc}); the run is intact at {run}")
    return 0 if rc == 0 else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--root", required=True, help="tree the manifest's relative paths are relative to")
    ap.add_argument("--manifest", required=True)
    ap.add_argument("--name", required=True)
    ap.add_argument("--bin", default="build/PGO/mtg", help="binary, relative to --root (default build/PGO/mtg)")
    ap.add_argument("--env", action="append", default=[], help="K=V for the run (repeatable)")
    ap.add_argument("--game-log-dir", default=None, help="relative to the run dir")
    ap.add_argument("--extra", default="", help="extra mtg arguments, one string")
    ap.add_argument("--publish", default=None, help="copy the outputs to this dir (relative to --root) at the end")
    ap.add_argument("--publish-existing", default=None, metavar="RUN", help="publish a finished run and exit")
    ap.add_argument("--launch", action="store_true")
    a = ap.parse_args() if "--publish-existing" not in sys.argv else None
    if a is None:
        ap2 = argparse.ArgumentParser()
        ap2.add_argument("--publish-existing", required=True)
        ap2.add_argument("--root", required=True)
        ap2.add_argument("--publish", required=True)
        b = ap2.parse_args()
        run = os.path.join(VOLUME, b.publish_existing)
        prov = json.load(open(os.path.join(run, "STAGED.json")))
        gld = prov.get("game_log_dir")
        cmd = prov.get("cmd") or []
        if gld is None and "--game-log-dir" in cmd:          # runs staged before game_log_dir was recorded
            gld = cmd[cmd.index("--game-log-dir") + 1]
        sys.exit(publish(run, os.path.join(os.path.realpath(b.root), b.publish), gld))

    root = os.path.realpath(a.root)
    run = os.path.join(VOLUME, a.name)
    if fstype(VOLUME) in ("", "9p", "drvfs"):
        sys.exit(f"vol_stage: {VOLUME} is not on a local volume (fstype={fstype(VOLUME)!r}) -- refusing")
    if os.path.exists(run) and os.listdir(run):
        sys.exit(f"vol_stage: {run} exists and is not empty -- pick a new --name")
    os.makedirs(run, exist_ok=True)

    def to_rel(p):
        """Manifest path -> (source abs path, path relative to the run dir)."""
        src = p if os.path.isabs(p) else os.path.join(root, p)
        src = os.path.normpath(src)
        if src.startswith(root + os.sep):
            return src, os.path.relpath(src, root)
        return src, os.path.join("ext", src.lstrip(os.sep))

    # binary
    bsrc = a.bin if os.path.isabs(a.bin) else os.path.join(root, a.bin)
    os.makedirs(os.path.join(run, "bin"), exist_ok=True)
    shutil.copy2(bsrc, os.path.join(run, "bin", "mtg"))
    stamp = os.path.join(os.path.dirname(bsrc), "SRC_TREE")
    bin_src_tree = open(stamp).read().strip() if os.path.exists(stamp) else ""

    # startup data files
    for rel, required in (("src/cards/data/cards.json", True), ("src/ai/data/heuristic_defaults.env", False)):
        s, d = os.path.join(root, rel), os.path.join(run, rel)
        os.makedirs(os.path.dirname(d), exist_ok=True)
        if os.path.exists(s):
            shutil.copy2(s, d)
        elif required:
            sys.exit(f"vol_stage: {s} missing")
        else:
            open(d, "w").close()          # block the upward walk; empty == absent

    # job inputs
    m = json.load(open(a.manifest if os.path.isabs(a.manifest) else os.path.join(root, a.manifest)))
    jobs = m["jobs"] if isinstance(m, dict) else m
    dirs = {}
    for j in jobs:
        for k, v in list(j.items()):
            if not isinstance(v, str) or k == "name":
                continue
            src, rel = to_rel(v)
            if not os.path.isfile(src):
                continue
            j[k] = rel
            dirs[os.path.dirname(src)] = os.path.dirname(rel)
    nfiles = 0
    for sdir, rdir in sorted(dirs.items()):
        ddir = os.path.join(run, rdir)
        os.makedirs(ddir, exist_ok=True)
        for f in sorted(os.listdir(sdir)):
            sp = os.path.join(sdir, f)
            if os.path.isfile(sp):        # follows symlinks; top level only
                shutil.copy2(sp, os.path.join(ddir, f), follow_symlinks=True)
                nfiles += 1
    json.dump(m, open(os.path.join(run, "manifest.json"), "w"), indent=1)

    env = dict(kv.split("=", 1) for kv in a.env)
    cmd = ["bin/mtg", "--batch", "manifest.json"]
    if a.game_log_dir:
        cmd += ["--game-log-dir", a.game_log_dir]
    if a.extra:
        cmd += a.extra.split()
    envs = " ".join(f"{k}={v}" for k, v in env.items())
    with open(os.path.join(run, "run.sh"), "w") as f:
        f.write("#!/usr/bin/env bash\n# generated by scripts/vol_stage.py -- runs entirely off the volume\n")
        f.write(f"cd {run}\n{('env ' + envs + ' ') if envs else ''}{' '.join(cmd)} > batch.out 2> batch.err\n")
        f.write('echo "rc=$?" >> batch.out\n')
        if a.publish:
            f.write(f"python3 {os.path.realpath(__file__)} --publish-existing {a.name} --root {root} "
                    f"--publish {a.publish} >> publish.log 2>&1\n")
    os.chmod(os.path.join(run, "run.sh"), 0o755)

    prov = {
        "root": root, "head": git(root, "rev-parse", "HEAD"), "src_tree": git(root, "rev-parse", "HEAD:src"),
        "src_dirty": bool(git(root, "status", "--porcelain", "--", "src")), "bin": bsrc,
        "bin_sha256": sha256(bsrc), "bin_src_tree": bin_src_tree, "manifest": a.manifest,
        "jobs": len(jobs), "input_dirs": len(dirs), "input_files": nfiles, "env": env, "cmd": cmd,
        "game_log_dir": a.game_log_dir, "publish": a.publish,
        "staged_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
    }
    json.dump(prov, open(os.path.join(run, "STAGED.json"), "w"), indent=1)
    if bin_src_tree and prov["src_tree"] and bin_src_tree != prov["src_tree"]:
        print(f"vol_stage: WARNING binary stamp {bin_src_tree[:12]} != {root} HEAD:src {prov['src_tree'][:12]}")
    if prov["src_dirty"]:
        print(f"vol_stage: WARNING {root}/src is dirty -- the binary may not match the tree")
    print(f"vol_stage: staged {len(jobs)} jobs, {nfiles} input files from {len(dirs)} dirs -> {run}")

    if a.launch:
        p = subprocess.Popen(["setsid", "nohup", "bash", os.path.join(run, "run.sh")],
                             stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                             cwd="/", start_new_session=True)
        open(os.path.join(run, "pid"), "w").write(f"{p.pid}\n")
        print(f"vol_stage: launched pid {p.pid}; output {run}/batch.out")


if __name__ == "__main__":
    main()
