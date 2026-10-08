"""Which engine binary a Python driver runs -- the Python twin of test/lib/harness.sh's harness_bin.

PGO+LTO FOR EVERYTHING EXCEPT QUICK DEVELOPMENT CYCLES (user, 2026-10-08: "we should be using PGO+LTO
for everything except maybe quick development cycles. It would also be good for screening changes.").
build/PGO (`./build.sh pgo`, scripts/build_pgo.sh) is ~1.3x faster per game and byte-identical to
build/Release -- same games, same digests (no -ffast-math, no -march) -- so it is used whenever it was
built from exactly the checked-out engine: build/PGO/SRC_TREE equals HEAD:src and src/ has no
uncommitted change. Otherwise (a development cycle) build/Release, with no build delay. A LONG run calls
pgo_ensure() first, which rebuilds build/PGO instead of falling back. MTG_PGO=0 pins Release everywhere;
use it on BOTH arms of any wall-clock comparison (PGO against Release measures the compiler).

    from engine_bin import engine_bin, pgo_ensure, kind
    pgo_ensure()                      # long runs only
    mtg = engine_bin("mtg")           # or engine_bin("mtg-analyze")

MTG_BIN / MTG_ANALYZE_BIN, when set, override (the regression harness's snapshot convention).
"""
import os
import platform
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def _git(*args):
    try:
        return subprocess.run(["git", "-C", ROOT] + list(args), capture_output=True, text=True).stdout.strip()
    except OSError:
        return ""


def _src_dirty():
    return bool(_git("status", "--porcelain", "--untracked-files=no", "--", "src"))


def pgo_fresh():
    if os.environ.get("MTG_PGO", "1") == "0":
        return False
    d = os.path.join(ROOT, "build", "PGO")
    stamp = os.path.join(d, "SRC_TREE")
    if not all(os.path.exists(p) for p in (stamp, os.path.join(d, "mtg"), os.path.join(d, "mtg-analyze"))):
        return False
    if open(stamp).read().strip() != _git("rev-parse", "HEAD:src"):
        return False
    return not _src_dirty()


def pgo_ensure():
    """Make build/PGO fresh for a long run, building it (~10-15 min) when it is not. Never fatal: a dirty
    src/, a non-Linux host or a failed build leaves the run on build/Release, said so on stderr."""
    if os.environ.get("MTG_PGO", "1") == "0" or pgo_fresh():
        return
    if platform.system() not in ("Linux", "Darwin"):
        return
    if _src_dirty():
        print("engine_bin: src/ has uncommitted changes -- running on build/Release "
              "(PGO builds only a committed tree)", file=sys.stderr)
        return
    print("engine_bin: build/PGO is not built from HEAD:src -- building it now (./build.sh pgo; "
          "MTG_PGO=0 skips)", file=sys.stderr, flush=True)
    rc = subprocess.call([os.path.join(ROOT, "build.sh"), "pgo"], cwd=ROOT, stdout=sys.stderr)
    if rc != 0:
        print("engine_bin: ./build.sh pgo FAILED -- running on build/Release", file=sys.stderr)


def engine_bin(name="mtg"):
    override = os.environ.get({"mtg": "MTG_BIN", "mtg-analyze": "MTG_ANALYZE_BIN"}.get(name, ""), "")
    if override:
        return override
    if pgo_fresh():
        return os.path.join(ROOT, "build", "PGO", name)
    for cand in (name, name + ".exe"):
        p = os.path.join(ROOT, "build", "Release", cand)
        if os.path.exists(p):
            return p
    return os.path.join(ROOT, "build", "Release", name)


def kind(path):
    return "PGO+LTO" if os.sep + "PGO" + os.sep in path or "/PGO/" in path else "Release"
