cmake_minimum_required(VERSION 3.20)
project(MagicDeckTester CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Opt-in deterministic hot-path profiling (see src/ai/Profiler.h). OFF by default
# so normal builds carry zero instrumentation overhead. Enable a separate build
# tree with:  cmake -S . -B build-prof -DMTG_PROFILE=ON
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

# Shared game engine library — linked by both the runner and the analyzer
add_library(mtg-core STATIC
    src/core/GameEngine.cpp
    src/core/GameLogger.cpp
    src/core/EffectHandler.cpp
    src/deck/DeckLoader.cpp
    src/ai/AIEngine.cpp
    src/ai/TurnSolver.cpp
    src/runner/GoldFishRunner.cpp
    src/runner/BatchRunner.cpp
    src/cards/CardDatabase.cpp
)
target_include_directories(mtg-core PUBLIC src)
target_link_libraries(mtg-core PUBLIC pugixml::pugixml nlohmann_json::nlohmann_json)
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
)
target_link_libraries(mtg-analyze PRIVATE mtg-core)
