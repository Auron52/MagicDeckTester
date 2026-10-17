#!/usr/bin/env bash
# Canonical build entry point for MagicDeckTester. USE THIS, not raw cmake.
#
# Why this exists: the binary path the harness/scripts use is build/<Config>/mtg. That path
# was once overwritten by a stray Unix-Makefiles `build/Release` dir configured with an EMPTY
# CMAKE_BUILD_TYPE -> NO optimization (-O0) -> a silent ~10x slowdown (it once turned a ~2h
# Hinata gen chunk into ~26h). This script drives the project's Ninja **Multi-Config** `build/`
# tree, whose Release/RelWithDebInfo configs are always optimized -- so an unoptimized binary
# cannot happen by mistake.
#
# Modes (optimized only):
#   release          -O3            -> build/Release/          (DEFAULT; use this for almost everything)
#   relwithdebinfo   -O2 + symbols  -> build/RelWithDebInfo/   (debugging a crash with a faithful stack)
#   profile          -O3 + symbols  -> build/Profile/          (faithful profiling: same codegen as Release + symbols)
#   pgo [deck-dir..] -O3 + PGO+LTO  -> build/PGO/              (~1.3x faster, byte-identical results; Linux only;
#                                                               trained on the whole suite, or on the named decks --
#                                                               scripts/build_pgo.sh. The engine for every LONG run:
#                                                               test/lib/harness.sh picks it while it matches HEAD:src)
#
# There is deliberately NO debug (-O0) mode here: an unoptimized build must be a separate,
# deliberate route (e.g. `cmake --build build --config Debug`), never the default.
#
# The Windows mirror of this script is build.cmd (-> build.ps1): same modes (except pgo, Linux-only), same build/<Config>/
# layout, same guards. Both configure through CMakePresets.json so there is one source of truth.
#
# Usage:
#   ./build.sh                       # release (optimized) -> build/Release
#   ./build.sh release
#   ./build.sh relwithdebinfo
#   ./build.sh profile
#   ./build.sh pgo                       # PGO+LTO, trained on the whole regression suite -> build/PGO
#   ./build.sh pgo decks/<Deck>          # PGO+LTO, trained on that deck only
#   ./build.sh <mode> -- --target mtg     # forward extra args to the build step
set -euo pipefail
cd "$(dirname "$0")"

mode="${1:-release}"
[ "$#" -gt 0 ] && shift

case "$mode" in
  release|Release)                        cfg=Release ;;
  relwithdebinfo|RelWithDebInfo|symbols)  cfg=RelWithDebInfo ;;
  profile|Profile)                        cfg=Profile ;;
  pgo|PGO)                                cfg=PGO ;;
  -h|--help|help)
    sed -n '2,30p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
  *)
    echo "build.sh: unknown mode '$mode'" >&2
    echo "  allowed (optimized only): release (default), relwithdebinfo, profile, pgo [deck-dir...]" >&2
    echo "  most tasks just need:  ./build.sh" >&2
    exit 2 ;;
esac

# Any args after a literal `--` are forwarded to the build step (e.g. --target mtg).
build_args=()
if [ "${1:-}" = "--" ]; then shift; build_args=("$@"); fi

# Prerequisites, checked BY NAME with an install hint. Letting cmake's own error surface
# ("No CMAKE_CXX_COMPILER could be found") tells a first-time user nothing about which of
# several things they are missing.
need() {
  command -v "$1" >/dev/null 2>&1 && return 0
  echo "build.sh: '$1' not found on PATH -- $2" >&2
  echo "  see docs/INSTALL.md" >&2
  exit 1
}
need cmake  "install it (Debian/Ubuntu: sudo apt install cmake)"
need ninja  "install it (Debian/Ubuntu: sudo apt install ninja-build)"
need git    "the build fetches pugixml/nlohmann_json/doctest via CMake FetchContent, which uses git"
if ! command -v c++ >/dev/null 2>&1 && ! command -v g++ >/dev/null 2>&1; then
  echo "build.sh: no C++ compiler on PATH -- install one (Debian/Ubuntu: sudo apt install g++)" >&2
  echo "  the engine is C++20; developed on g++ 13, and g++ >= 11 / clang >= 14 should work." >&2
  echo "  see docs/INSTALL.md" >&2
  exit 1
fi

# Compile output stays OFF the 9p host mount (USER rule, 2026-10-10). In the devcontainer the checkout is a
# 9p/drvfs bind of the Windows drive: one connection shared by every container, with no reconnect. On 10-10 it
# died under heavy agent build + batch I/O (four worktrees compiling into their own build/ on it) and took both
# containers down for hours. Compile output is regenerable, so it belongs on the ext4 build volume mounted at the
# primary checkout's build/; source, commits and results stay on the host drive, which is what keeps them safe.
# A linked worktree on 9p therefore gets build/ -> <primary>/build/wt/<worktree> before anything compiles. An
# existing 9p build/ is renamed to build.9p-old (kept, not deleted). Nothing changes off 9p or in the primary.
if [ ! -L build ] && [ "$(stat -f -c %T . 2>/dev/null)" = v9fs ] \
   && { [ ! -e build ] || [ "$(stat -f -c %T build 2>/dev/null)" = v9fs ]; }; then
  common="$(git rev-parse --path-format=absolute --git-common-dir 2>/dev/null || true)"
  primary="$(dirname "${common:-/nonexistent/.git}")"
  if [ -n "$common" ] && [ "$primary" != "$(pwd -P)" ] && [ -d "$primary/build" ] \
     && [ "$(stat -f -c %T "$primary/build")" != v9fs ]; then
    vol="$primary/build/wt/$(basename "$(pwd -P)")"
    if [ -e build ]; then
      if [ -e build.9p-old ]; then
        echo "build.sh: build/ is on the 9p host mount and build.9p-old already exists; move one of them aside" >&2
        exit 1
      fi
      mv build build.9p-old
    fi
    mkdir -p "$vol"
    ln -s "$vol" build
    # The build/ ignore pattern matches directories only, so git would list the symlink as untracked.
    ex="$common/info/exclude"
    grep -qx '/build' "$ex" 2>/dev/null || echo '/build' >> "$ex"
    grep -qx '/build.9p-old/' "$ex" 2>/dev/null || echo '/build.9p-old/' >> "$ex"
    echo ">>> build/ -> $vol (ext4 build volume; compile output stays off the 9p host mount)"
  else
    echo "build.sh: WARNING: build/ is on the 9p host mount and no ext4 build volume was found at the" >&2
    echo "  primary checkout's build/. Heavy compile I/O on 9p can kill the mount for every container." >&2
  fi
fi
# A build dir outside the tree must still find THIS tree's src/ai/data/heuristic_defaults.env: the engine
# resolves it by walking UP from the binary's real path (HeuristicDefaults.h), which from
# <primary>/build/wt/<name>/<Config>/ would otherwise reach the PRIMARY checkout's copy -- silently the
# wrong tree. A `src` link beside the configs makes the walk stop at this tree's file first.
# `decks` the same way: the unit tests that load a decklist (ResolveHeuristicDefaultsPath("decks/...")),
# among them the every-deck routing table, otherwise read the PRIMARY checkout's lists -- 2026-10-17 a
# rebased worktree failed that table on a deck folder the primary had not pulled yet.
if [ -L build ]; then
  bt="$(readlink -f build)"
  if [ -L "$bt/src" ] || [ ! -e "$bt/src" ]; then ln -sfn "$PWD/src" "$bt/src"; fi
  if [ -L "$bt/decks" ] || [ ! -e "$bt/decks" ]; then ln -sfn "$PWD/decks" "$bt/decks"; fi
fi

# PGO+LTO mode: its own single-config tree (build/pgo-work/ -> binaries in build/PGO/), trained on the
# named decks. Explicit Release type, so it can never be the -O0 tree the guard below exists for.
# See scripts/build_pgo.sh for the why/how and the SRC_TREE stamp scripts/mullgen.sh checks.
if [ "$cfg" = PGO ]; then exec bash scripts/build_pgo.sh "$@"; fi

# Stale single-config tree guard. build/<Config>/ is an OUTPUT dir of the multi-config generator,
# NOT a build tree -- a CMakeCache.txt there came from a bare `cmake -S . -B build/Release`, which
# leaves CMAKE_BUILD_TYPE empty and compiles at -O0 (the ~10x slowdown in the header). Refuse
# rather than let the two trees fight over the same binary path.
for c in Release RelWithDebInfo Profile; do
  if [ -f "build/$c/CMakeCache.txt" ]; then
    echo "build.sh: stale single-config CMake tree at build/$c/" >&2
    echo "  build/<Config>/ is an OUTPUT directory of the multi-config generator, not a build tree." >&2
    echo "  A CMakeCache.txt there came from a bare 'cmake -S . -B build/$c', which compiles with" >&2
    echo "  NO optimization (~10x slower). Delete it and re-run:  rm -rf build/$c" >&2
    exit 1
  fi
done

# Configure/regenerate the Multi-Config tree. Always regenerate (cheap when nothing changed) so a
# newly-added config -- e.g. Profile -- gets its per-config ninja file before we build it. Reuse the
# existing generator if already configured; otherwise configure from CMakePresets.json (the same
# preset file the Windows build and the CI matrix use, so all three agree by construction).
if [ -f build/CMakeCache.txt ]; then
  cmake -S . -B build >/dev/null
else
  echo ">>> configuring build/ (preset: linux -- Ninja Multi-Config)"
  cmake --preset linux >/dev/null
fi

jobs="$(nproc 2>/dev/null || echo 4)"
echo ">>> build: $cfg on $jobs jobs -> build/$cfg/"
cmake --build build --config "$cfg" -j"$jobs" "${build_args[@]}"
echo ">>> done: build/$cfg/  (config=$cfg, optimized)"
