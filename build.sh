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
#
# There is deliberately NO debug (-O0) mode here: an unoptimized build must be a separate,
# deliberate route (e.g. `cmake --build build --config Debug`), never the default.
#
# Usage:
#   ./build.sh                       # release (optimized) -> build/Release
#   ./build.sh release
#   ./build.sh relwithdebinfo
#   ./build.sh profile
#   ./build.sh <mode> -- --target mtg     # forward extra args to the build step
set -euo pipefail
cd "$(dirname "$0")"

mode="${1:-release}"
[ "$#" -gt 0 ] && shift

case "$mode" in
  release|Release)                        cfg=Release ;;
  relwithdebinfo|RelWithDebInfo|symbols)  cfg=RelWithDebInfo ;;
  profile|Profile)                        cfg=Profile ;;
  -h|--help|help)
    sed -n '2,25p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
  *)
    echo "build.sh: unknown mode '$mode'" >&2
    echo "  allowed (optimized only): release (default), relwithdebinfo, profile" >&2
    echo "  most tasks just need:  ./build.sh" >&2
    exit 2 ;;
esac

# Any args after a literal `--` are forwarded to the build step (e.g. --target mtg).
build_args=()
if [ "${1:-}" = "--" ]; then shift; build_args=("$@"); fi

# Configure/regenerate the Multi-Config tree. Always regenerate (cheap when nothing changed) so a
# newly-added config -- e.g. Profile -- gets its per-config ninja file before we build it. Reuse the
# existing generator if already configured; otherwise create it as Ninja Multi-Config.
if [ -f build/CMakeCache.txt ]; then
  cmake -S . -B build >/dev/null
else
  echo ">>> configuring build/ (Ninja Multi-Config)"
  cmake -S . -B build -G "Ninja Multi-Config" >/dev/null
fi

jobs="$(nproc 2>/dev/null || echo 4)"
echo ">>> build: $cfg on $jobs jobs -> build/$cfg/"
cmake --build build --config "$cfg" -j"$jobs" "${build_args[@]}"
echo ">>> done: build/$cfg/  (config=$cfg, optimized)"
