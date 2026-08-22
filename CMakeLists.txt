cmake_minimum_required(VERSION 3.20)

# Guard against the silent -O0 footgun: a bare `cmake -S . -B build/Release` with no
# -DCMAKE_BUILD_TYPE leaves the type EMPTY, which compiles with NO optimization (~10x
# slower -- this cost us a ~26h Hinata gen chunk that should have been ~2h). Default an
# unset type to Release so even an unqualified configure is optimized. The canonical
# entry point is ./build.sh, which always sets an explicit optimized build type.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING
      "Build type (Release / RelWithDebInfo)" FORCE)
  message(STATUS "CMAKE_BUILD_TYPE was unset -> defaulting to Release (optimized). "
                 "Prefer ./build.sh to choose a mode.")
endif()

project(MagicDeckTester CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# A "Profile" configuration = Release (-O3) + debug symbols, for FAITHFUL profiling: -g does not
# change codegen, so this is instruction-identical to the shipped Release binary plus symbols. It is
# kept SEPARATE from RelWithDebInfo (which stays CMake-standard -O2 -g for friendlier crash stacks),
# so profiling reflects the real binary while debugging keeps its faithful stacks. Registered on
# multi-config generators (Ninja Multi-Config / MSVC) as `--config Profile`; `./build.sh profile` uses it.
if(CMAKE_CONFIGURATION_TYPES AND NOT "Profile" IN_LIST CMAKE_CONFIGURATION_TYPES)
  list(APPEND CMAKE_CONFIGURATION_TYPES Profile)
  set(CMAKE_CONFIGURATION_TYPES "${CMAKE_CONFIGURATION_TYPES}" CACHE STRING "" FORCE)
endif()

# Multi-config side of the -O0 guard above (the `if(NOT CMAKE_BUILD_TYPE ...)` branch never runs
# when CMAKE_CONFIGURATION_TYPES is set). Ninja Multi-Config builds the FIRST listed config when
# `cmake --build build` is given no --config, and the generator's stock list starts with Debug --
# so a bare build silently produced -O0 objects, and compile_commands.json was emitted with the
# Debug (-O0) flags. Drop Debug from the offered configs (CLAUDE.md: there is deliberately no -O0
# mode; a debug build must be a separate single-config tree, e.g.
# `cmake -DCMAKE_BUILD_TYPE=Debug -B build/Debug`) and pin the default config to Release as a
# backstop in case Debug is ever re-added.
if(CMAKE_CONFIGURATION_TYPES AND "Debug" IN_LIST CMAKE_CONFIGURATION_TYPES)
  list(REMOVE_ITEM CMAKE_CONFIGURATION_TYPES Debug)
  set(CMAKE_CONFIGURATION_TYPES "${CMAKE_CONFIGURATION_TYPES}" CACHE STRING "" FORCE)
  message(STATUS "mtg: removed Debug (-O0) from CMAKE_CONFIGURATION_TYPES -- use ./build.sh")
endif()
# CMAKE_DEFAULT_BUILD_TYPE is a NINJA MULTI-CONFIG-ONLY variable: the Visual Studio generators
# reject it outright ("does not support variable CMAKE_DEFAULT_BUILD_TYPE but it has been
# specified") and fail the generate step, so it must be scoped to the generator that has it.
# Nothing is lost on Windows -- a VS generator with no --config builds the FIRST entry of
# CMAKE_CONFIGURATION_TYPES, and both the preset and the list above put Release first, which is
# the same protection by ordering.
if(CMAKE_GENERATOR STREQUAL "Ninja Multi-Config" AND NOT CMAKE_DEFAULT_BUILD_TYPE)
  set(CMAKE_DEFAULT_BUILD_TYPE Release CACHE STRING
      "Config built by a bare `cmake --build build` (no --config)" FORCE)
endif()
# Profile config flags, per toolchain. GCC/Clang spell "optimize + symbols" as -O3 -g; MSVC as
# /O2 /Zi with /DEBUG on the link. An unguarded -O3 here made the whole Windows configure fail.
if(MSVC)
  set(CMAKE_CXX_FLAGS_PROFILE "/O2 /Zi /DNDEBUG" CACHE STRING "Release + debug symbols (faithful profiling)")
  set(CMAKE_C_FLAGS_PROFILE   "/O2 /Zi /DNDEBUG" CACHE STRING "Release + debug symbols (faithful profiling)")
  set(CMAKE_EXE_LINKER_FLAGS_PROFILE    "/DEBUG /INCREMENTAL:NO" CACHE STRING "")
  set(CMAKE_SHARED_LINKER_FLAGS_PROFILE "/DEBUG /INCREMENTAL:NO" CACHE STRING "")
else()
  set(CMAKE_CXX_FLAGS_PROFILE "-O3 -g -DNDEBUG" CACHE STRING "Release + debug symbols (faithful profiling)")
  set(CMAKE_C_FLAGS_PROFILE   "-O3 -g -DNDEBUG" CACHE STRING "Release + debug symbols (faithful profiling)")
  set(CMAKE_EXE_LINKER_FLAGS_PROFILE    "" CACHE STRING "")
  set(CMAKE_SHARED_LINKER_FLAGS_PROFILE "" CACHE STRING "")
endif()
set(CMAKE_STATIC_LINKER_FLAGS_PROFILE "" CACHE STRING "")
mark_as_advanced(CMAKE_CXX_FLAGS_PROFILE CMAKE_C_FLAGS_PROFILE
  CMAKE_EXE_LINKER_FLAGS_PROFILE CMAKE_SHARED_LINKER_FLAGS_PROFILE CMAKE_STATIC_LINKER_FLAGS_PROFILE)

# Opt-in deterministic hot-path profiling (see src/ai/Profiler.h). OFF by default
# so normal builds carry zero instrumentation overhead. NOTE: this is the COUNTER
# instrumentation, distinct from the Profile config above (-O3 -g, uninstrumented,
# via `./build.sh profile`). If counters are needed, configure a separate tree:
#   cmake -S . -B build-instr -DMTG_PROFILE=ON -DCMAKE_BUILD_TYPE=Release
option(MTG_PROFILE "Enable deterministic search profiling counters" OFF)

include(FetchContent)

FetchContent_Declare(
    pugixml
    GIT_REPOSITORY https://github.com/zeux/pugixml.git
    GIT_TAG        v1.14
)
FetchContent_MakeAvailable(pugixml)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# Build-time engine fingerprint (see cmake/EngineFingerprint.cmake and
# docs/design/execution-trace-carry.md, Phase B). Regenerated on every build via a custom
# command so it tracks the real source on disk; only rewrites the header when it changes.
# mtg-core depends on it and exposes the generated include dir PUBLIC-ly, so both the runner
# and the analyzer see MTG_ENGINE_FP.
set(MTG_GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(MTG_ENGINE_FP_HEADER "${MTG_GENERATED_DIR}/engine_fp.h")
file(MAKE_DIRECTORY "${MTG_GENERATED_DIR}")
add_custom_target(mtg_engine_fp
    BYPRODUCTS "${MTG_ENGINE_FP_HEADER}"
    COMMAND ${CMAKE_COMMAND}
            -DSRC_DIR=${CMAKE_SOURCE_DIR}/src
            -DOUT_FILE=${MTG_ENGINE_FP_HEADER}
            -P ${CMAKE_SOURCE_DIR}/cmake/EngineFingerprint.cmake
    COMMENT "Computing engine fingerprint (MTG_ENGINE_FP)"
    VERBATIM)

# Build-time MTG_* flag registry (same pattern): scanned from the source literals so the startup
# typo validator (src/core/FlagRegistry.cpp) and --list-flags can never drift from the code.
set(MTG_FLAG_REGISTRY_HEADER "${MTG_GENERATED_DIR}/flag_registry.h")
add_custom_target(mtg_flag_registry
    BYPRODUCTS "${MTG_FLAG_REGISTRY_HEADER}"
    COMMAND ${CMAKE_COMMAND}
            -DSRC_DIR=${CMAKE_SOURCE_DIR}/src
            -DOUT_FILE=${MTG_FLAG_REGISTRY_HEADER}
            -P ${CMAKE_SOURCE_DIR}/cmake/FlagRegistry.cmake
    COMMENT "Generating MTG_* flag registry"
    VERBATIM)

# Shared game engine library — linked by both the runner and the analyzer
add_library(mtg-core STATIC
    src/core/GameEngine.cpp
    src/core/SpellEffects.cpp
    src/core/GameLogger.cpp
    src/core/EffectHandler.cpp
    src/deck/DeckLoader.cpp
    src/ai/AIEngine.cpp
    src/ai/DecisionProviders.cpp
    src/ai/TurnSolver.cpp
    src/ai/ManaPayment.cpp
    src/ai/PlanContext.cpp
    src/ai/LandPlay.cpp
    src/ai/Combat.cpp
    src/ai/KeepModel.cpp
    src/runner/GoldFishRunner.cpp
    src/runner/BatchRunner.cpp
    src/cards/CardDatabase.cpp
    src/core/NameRegistry.cpp
    src/core/PoolAllocator.cpp
    src/core/FlagRegistry.cpp
)
target_include_directories(mtg-core PUBLIC src "${MTG_GENERATED_DIR}")
target_link_libraries(mtg-core PUBLIC pugixml::pugixml nlohmann_json::nlohmann_json)
add_dependencies(mtg-core mtg_engine_fp mtg_flag_registry)

# MSVC settings that are REQUIRED, not stylistic -- each one is a hard build failure without it:
#   /bigobj     src/ai/TurnSolver.cpp is ~25k lines and blows past COFF's 65,535-section limit.
#   /utf-8      23 sources contain non-ASCII (em-dashes, arrows) in comments and literals; MSVC
#               otherwise decodes them in the system codepage (C4819, and mangled literals).
#   /EHsc       the engine uses exceptions (nlohmann::json throws); MSVC's default is no model.
#   /permissive- conformance, so the code that GCC accepts is held to the same standard rules.
# PUBLIC so the runner, analyzer and unit tests inherit them (they compile the same headers).
if(MSVC)
    target_compile_options(mtg-core PUBLIC /bigobj /utf-8 /EHsc /permissive-)
endif()

# zlib, so the runtime can read gzipped deck profiles (.json.gz). This is NOT optional in
# practice: EVERY deck ships its exhaustive keep/bottom mulligan table as
# decks/<name>/<name>.keepmodel.exhaustive.profile.json.gz (the raw table is ~13 MB; gzip -> ~1-2 MB).
# A build without zlib reads those as an EMPTY default profile, so the deck silently plays
# different mulligans -- a behaviour divergence with no visible cause. That is exactly what a
# stock Windows box (no system zlib, no vcpkg) used to get.
#
# So: prefer the system zlib, and FALL BACK to building it via FetchContent when absent. Either
# way MTG_HAVE_ZLIB is defined and .gz reading works, on every platform, with no user setup.
# -DMTG_FETCH_ZLIB=OFF opts out of the fallback (the no-zlib path then warns loudly at runtime --
# see ReadProfileText in src/ai/MulliganProfileIO.h).
option(MTG_FETCH_ZLIB "Build zlib via FetchContent when the system has none" ON)
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    message(STATUS "mtg: linking zlib (${ZLIB_VERSION_STRING}) -- gzipped profiles enabled")
    target_link_libraries(mtg-core PUBLIC ZLIB::ZLIB)
    target_compile_definitions(mtg-core PUBLIC MTG_HAVE_ZLIB)
elseif(MTG_FETCH_ZLIB)
    message(STATUS "mtg: no system zlib -- building it via FetchContent (gzipped profiles enabled)")
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        zlib
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG        v1.3.1
    )
    FetchContent_MakeAvailable(zlib)
    # zlib's own CMake exports `zlibstatic` (and `zlib` for the shared build) but no ZLIB::ZLIB,
    # and its generated zconf.h lands in the BINARY dir -- both include dirs are needed.
    target_include_directories(mtg-core PUBLIC "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")
    target_link_libraries(mtg-core PUBLIC zlibstatic)
    target_compile_definitions(mtg-core PUBLIC MTG_HAVE_ZLIB)
else()
    message(WARNING "mtg: zlib not found and MTG_FETCH_ZLIB=OFF -- gzipped mulligan tables will "
                    "NOT be read; decks fall back to default mulligans (the runtime warns).")
endif()

if(MTG_PROFILE)
    target_compile_definitions(mtg-core PUBLIC MTG_PROFILE)
endif()

# Optional: a fast general allocator. The simulator is malloc/free-bound on the search
# hot path (the per-node GameState deep-copy is millions of small short-lived
# allocations), so a small-object-pool allocator cuts a large slice of wall time. Linked
# CONDITIONALLY and allocator-agnostically so the build works on any platform with or
# without one -- it only changes WHERE memory lives, never the data/logic, so results stay
# byte-identical regardless of which (or no) allocator each platform uses.
#
# Preference order: jemalloc (first-class on Linux/Unix; the dev container installs
# libjemalloc-dev, so a rebuild turns it on) -> mimalloc (first-class on Windows AND
# cross-platform, so a future MSVC build can opt in) -> the system allocator. Override
# with -DMTG_USE_FAST_ALLOC=OFF.
#
# NOTE (Windows): linking mimalloc overrides new/malloc automatically on Linux via symbol
# interposition; an MSVC build additionally needs mimalloc's redirect/override mechanism
# (mimalloc-redirect.dll, or the mimalloc-new-delete header for static linking) -- wire
# that up when the Windows build is actually set up.
option(MTG_USE_FAST_ALLOC "Link a fast allocator (jemalloc, else mimalloc) if available" ON)
if(MTG_USE_FAST_ALLOC)
    find_library(JEMALLOC_LIB jemalloc)
    if(JEMALLOC_LIB)
        message(STATUS "mtg: linking jemalloc (${JEMALLOC_LIB})")
        target_link_libraries(mtg-core PUBLIC ${JEMALLOC_LIB})
    else()
        # Prefer mimalloc's own CMake package (exports the `mimalloc` target), else a bare lib.
        find_package(mimalloc CONFIG QUIET)
        if(TARGET mimalloc)
            message(STATUS "mtg: linking mimalloc (CMake package)")
            target_link_libraries(mtg-core PUBLIC mimalloc)
        else()
            find_library(MIMALLOC_LIB mimalloc)
            if(MIMALLOC_LIB)
                message(STATUS "mtg: linking mimalloc (${MIMALLOC_LIB})")
                target_link_libraries(mtg-core PUBLIC ${MIMALLOC_LIB})
            else()
                message(STATUS "mtg: no fast allocator found -- using the system allocator")
            endif()
        endif()
    endif()
endif()

# Runner: goldfish simulator
add_executable(mtg src/main.cpp)
target_link_libraries(mtg PRIVATE mtg-core)

# Analyzer: search-based deck analysis, outputs DeckProfile JSON
add_executable(mtg-analyze
    src/analyzer/main.cpp
    src/analyzer/AnalyzerEngine.cpp
    src/analyzer/KeepModelTrainer.cpp
    src/analyzer/EquivalenceDiscovery.cpp
    src/analyzer/ExhaustiveKeep.cpp
)
target_link_libraries(mtg-analyze PRIVATE mtg-core)

# Unit tests (backlog C2): a SECONDS-long faithfulness check for the units the executor/rollout
# unification (Tier C) touches -- the cheap seam the 15-minute smoke run cannot provide. Scope is
# deliberately tiny: golden-value tests for twin units only, not a general test suite.
#   ctest --test-dir build -C Release        (or just run build/Release/mtg-test)
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
)
# doctest v2.4.11's own CMakeLists declares a cmake_minimum_required below 3.5. CMake 3.x only
# WARNS about that; CMake >= 4.0 REMOVED the compatibility and errors out, which is a hard
# configure failure on any machine with a current CMake (it is what broke the first Windows CI
# run, whose runner ships CMake 4.x -- our container's 3.28 merely warned, so this was invisible
# locally). CMAKE_POLICY_VERSION_MINIMUM is CMake's own documented escape hatch. Scoped to this
# dependency and unset immediately after, so OUR policy scope is untouched -- the other three
# FetchContent deps configure clean and must not be silently relaxed too.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
FetchContent_MakeAvailable(doctest)
unset(CMAKE_POLICY_VERSION_MINIMUM)
enable_testing()
add_executable(mtg-test
    test/unit/test_mana_payment.cpp
)
target_link_libraries(mtg-test PRIVATE mtg-core doctest::doctest)
add_test(NAME unit COMMAND mtg-test)
