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


_warned = [False]


def pgo_stale_reason():
    """Why pgo_fresh() said no, or "" when PGO is fresh / was never built here. Mirrors
    harness_pgo_stale_reason in test/lib/harness.sh -- keep the two in step."""
    if os.environ.get("MTG_PGO", "1") == "0":
        return ""
    d = os.path.join(ROOT, "build", "PGO")
    stamp = os.path.join(d, "SRC_TREE")
    if not os.path.exists(stamp):
        return ""                                  # never built here: an ordinary dev cycle
    if pgo_fresh():
        return ""
    if not all(os.path.exists(os.path.join(d, b)) for b in ("mtg", "mtg-analyze")):
        return "build/PGO is stamped but its binaries are missing"
    have, want = open(stamp).read().strip(), _git("rev-parse", "HEAD:src")
    if have != want:
        return "build/PGO was built from src %s but HEAD:src is now %s" % (have, want)
    if _src_dirty():
        return "src/ has uncommitted changes, so the PGO stamp cannot be trusted"
    return "build/PGO is not usable"


def pgo_warn_once():
    """Say so, ONCE, when a stale build/PGO is being stepped over. See the long note in
    test/lib/harness.sh: a silent fallback measured a whole screen on the wrong engine. Raises under
    MTG_PGO_REQUIRE=1 so a measurement chain fails rather than switching engine behind its back."""
    why = pgo_stale_reason()
    if not why:
        return
    if not _warned[0]:
        _warned[0] = True
        print("engine_bin: %s -- FALLING BACK TO build/Release.\n"
              "engine_bin: MTG_PGO=1 selects PGO, it does not force it. Rebuild (./build.sh pgo)\n"
              "engine_bin: before measuring, or set MTG_PGO_REQUIRE=1 to fail instead." % why,
              file=sys.stderr, flush=True)
    if os.environ.get("MTG_PGO_REQUIRE", "0") != "0":
        raise RuntimeError("MTG_PGO_REQUIRE=1 and build/PGO is not usable: %s" % why)


def engine_bin(name="mtg"):
    override = os.environ.get({"mtg": "MTG_BIN", "mtg-analyze": "MTG_ANALYZE_BIN"}.get(name, ""), "")
    if override:
        return override
    if pgo_fresh():
        return os.path.join(ROOT, "build", "PGO", name)
    pgo_warn_once()
    for cand in (name, name + ".exe"):
        p = os.path.join(ROOT, "build", "Release", cand)
        if os.path.exists(p):
            return p
    return os.path.join(ROOT, "build", "Release", name)


def kind(path):
    return "PGO+LTO" if os.sep + "PGO" + os.sep in path or "/PGO/" in path else "Release"
