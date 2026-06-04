cmake_minimum_required(VERSION 3.20)
project(MagicDeckTester CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
    src/cards/CardDatabase.cpp
)
target_include_directories(mtg-core PUBLIC src)
target_link_libraries(mtg-core PUBLIC pugixml::pugixml nlohmann_json::nlohmann_json)

# Runner: goldfish simulator
add_executable(mtg src/main.cpp)
target_link_libraries(mtg PRIVATE mtg-core)

# Analyzer: search-based deck analysis, outputs DeckProfile JSON
add_executable(mtg-analyze
    src/analyzer/main.cpp
    src/analyzer/AnalyzerEngine.cpp
)
target_link_libraries(mtg-analyze PRIVATE mtg-core)
