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

add_executable(mtg
    src/main.cpp
    src/core/GameEngine.cpp
    src/deck/DeckLoader.cpp
    src/ai/AIEngine.cpp
    src/runner/GoldFishRunner.cpp
)

target_include_directories(mtg PRIVATE src)
target_link_libraries(mtg PRIVATE pugixml::pugixml)
