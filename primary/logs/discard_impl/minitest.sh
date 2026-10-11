#!/usr/bin/env bash
# Low-contention unit-test loop for ONE worktree's DecisionProviders.cpp + ONE test file.
#
#   minitest.sh <worktree-abs-path> <test/unit/test_x.cpp> [doctest args...]
#   minitest.sh <worktree-abs-path> --syntax          # -fsyntax-only on DecisionProviders.cpp only
#
# Compiles the worktree's src/ai/DecisionProviders.cpp and the test file at -O1 (niced, pinned to
# cores 0-5), links them against the PRIMARY checkout's Release libmtg-core.a with its
# DecisionProviders object removed, plus the primary build's doctest main (test_mana_payment.cpp.o),
# and runs only the given test file's cases. Never a full build.
#
# CONSTRAINTS this relies on (the rest of libmtg-core was compiled against the PRIMARY headers):
#   * do not add DATA MEMBERS to any class, and do not add NEW virtual functions to DecisionProvider
#     (overriding an existing virtual, or adding a non-virtual member/static helper, is fine);
#   * do not edit any header other than src/ai/DecisionProviders.h (HeuristicArm.h already holds the
#     arms; comment-only edits there are harmless).
set -euo pipefail
PRIMARY=/workspaces/MagicDeckTester
WT="${1:?worktree path}"; shift
WT="$(cd "$WT" && pwd)"
OUT="$WT/logs/minitest"; mkdir -p "$OUT"
CXX=/usr/bin/c++
INC=(-DMTG_HAVE_ZLIB -I"$WT/src" -I"$PRIMARY/build/generated" -I"$PRIMARY/build/_deps/pugixml-src/src"
     -I"$PRIMARY/build/_deps/nlohmann_json-src/include" -I"$PRIMARY/build/_deps/doctest-src" -std=gnu++20 -DNDEBUG)
RUN=(nice -n 19 taskset -c 0)

if [[ "${1:-}" == "--syntax" ]]; then
    "${RUN[@]}" "$CXX" "${INC[@]}" -fsyntax-only "$WT/src/ai/DecisionProviders.cpp"
    echo "syntax OK"; exit 0
fi
TEST="${1:?test file (relative to the worktree)}"; shift

NODP="$PRIMARY/logs/discard_impl/libmtg-core-noDP.a"
LIB="$PRIMARY/build/Release/libmtg-core.a"
if [[ ! -f "$NODP" || "$LIB" -nt "$NODP" ]]; then
    ( flock 9
      if [[ ! -f "$NODP" || "$LIB" -nt "$NODP" ]]; then
          cp "$LIB" "$NODP.tmp" && ar d "$NODP.tmp" DecisionProviders.cpp.o && mv "$NODP.tmp" "$NODP"
      fi ) 9>"$PRIMARY/logs/discard_impl/.nodp.lock"
fi

DP_SRC="$WT/src/ai/DecisionProviders.cpp"
DP_O="$OUT/DecisionProviders.o"
# Rebuild the provider object only when the .cpp or .h changed since the last compile.
if [[ ! -f "$DP_O" || "$DP_SRC" -nt "$DP_O" || "$WT/src/ai/DecisionProviders.h" -nt "$DP_O" || "$NODP" -nt "$DP_O" ]]; then
    echo ">>> compiling DecisionProviders.cpp (-O1, niced)"
    "${RUN[@]}" "$CXX" "${INC[@]}" -O1 -c "$DP_SRC" -o "$DP_O"
fi
# MINITEST_EXTRA="src/ai/TurnSolver.cpp ..." -- further engine sources this worktree changes: each is
# compiled here (-O1, cached by mtime) and its object is dropped from a per-worktree copy of the library.
LIBUSE="$NODP"; EXTRA_O=()
if [[ -n "${MINITEST_EXTRA:-}" ]]; then
    LIBUSE="$OUT/libmtg-core-custom.a"
    cp "$NODP" "$LIBUSE"
    for src in $MINITEST_EXTRA; do
        base="$(basename "$src")"
        ar d "$LIBUSE" "$base.o" 2>/dev/null || true
        eo="$OUT/${base%.cpp}.o"
        if [[ ! -f "$eo" || "$WT/$src" -nt "$eo" || "$NODP" -nt "$eo" ]]; then
            echo ">>> compiling $src (-O1, niced)"
            "${RUN[@]}" "$CXX" "${INC[@]}" -O1 -c "$WT/$src" -o "$eo"
        fi
        EXTRA_O+=("$eo")
    done
fi
T_O="$OUT/$(basename "$TEST" .cpp).o"
echo ">>> compiling $TEST"
"${RUN[@]}" "$CXX" "${INC[@]}" -O1 -c "$WT/$TEST" -o "$T_O"
MAIN_O="$PRIMARY/build/CMakeFiles/mtg-test.dir/Release/test/unit/test_mana_payment.cpp.o"
BIN="$OUT/mtg-minitest"
"${RUN[@]}" "$CXX" -o "$BIN" "$MAIN_O" "$DP_O" ${EXTRA_O[@]+"${EXTRA_O[@]}"} "$T_O" "$LIBUSE" \
    "$PRIMARY/build/_deps/pugixml-build/Release/libpugixml.a" -lz -lpthread
cd "$WT"
exec "$BIN" --source-file="*$(basename "$TEST")" "$@"
