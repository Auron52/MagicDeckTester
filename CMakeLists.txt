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

# Optional: jemalloc. The simulator is malloc/free-bound on the search hot path (the
# per-node GameState deep-copy is millions of small short-lived allocations). jemalloc's
# small-object pools cut that substantially. Linked CONDITIONALLY so the build works with
# OR without it: the dev container installs libjemalloc-dev (see .devcontainer/Dockerfile),
# and a rebuild that includes that layer turns this on automatically. It only changes where
# memory lives, never the data/logic, so simulation results stay byte-identical.
# Override with -DMTG_USE_JEMALLOC=OFF to force the system allocator.
option(MTG_USE_JEMALLOC "Link jemalloc if available" ON)
if(MTG_USE_JEMALLOC)
    find_library(JEMALLOC_LIB jemalloc)
    if(JEMALLOC_LIB)
        message(STATUS "mtg: linking jemalloc (${JEMALLOC_LIB})")
        target_link_libraries(mtg-core PUBLIC ${JEMALLOC_LIB})
    else()
        message(STATUS "mtg: jemalloc not found -- using the system allocator")
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
