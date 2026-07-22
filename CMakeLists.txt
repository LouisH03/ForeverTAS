cmake_minimum_required(VERSION 3.21)

project(ForeverTAS LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

set(FOREVERVALIDATOR_BUILD_CLI OFF CACHE BOOL
    "Do not build the embedded ForeverValidator CLI" FORCE)
set(FOREVERVALIDATOR_BUILD_TESTS OFF CACHE BOOL
    "Do not build embedded ForeverValidator tests" FORCE)

FetchContent_Declare(
    ForeverValidator
    GIT_REPOSITORY https://github.com/Skycrafter-dev/ForeverValidator.git
    GIT_TAG 0a287aa7a0748faf5fffe9b8955505b8b500a5ce
    GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(ForeverValidator)

add_library(forevertas_core STATIC
    src/evaluators/max_speed_evaluator.cpp
    src/mutations/random_steering_mutator.cpp
    src/searches/serial_brute_force_search.cpp)
target_include_directories(forevertas_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(forevertas_core PUBLIC ForeverValidator::core)

add_executable(forevertas src/app/main.cpp)
target_link_libraries(forevertas PRIVATE
    forevertas_core
    ForeverValidator::native)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(forevertas_core PRIVATE -Wall -Wextra -Werror)
    target_compile_options(forevertas PRIVATE -Wall -Wextra -Werror)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(forevertas-strategy-tests tests/strategy_tests.cpp)
    target_link_libraries(forevertas-strategy-tests PRIVATE forevertas_core)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(forevertas-strategy-tests PRIVATE
            -Wall -Wextra -Werror)
    endif()

    add_test(NAME forevertas-help
             COMMAND forevertas --help)
    add_test(NAME forevertas-strategies
             COMMAND forevertas-strategy-tests)

    set(FOREVERTAS_TEST_PACK_DIRECTORY "" CACHE PATH
        "Installed TMUF Packs directory used by the optional search smoke test")
    set(FOREVERTAS_TEST_REPLAY "" CACHE FILEPATH
        "Replay used by the optional search smoke test")
    if(FOREVERTAS_TEST_PACK_DIRECTORY AND FOREVERTAS_TEST_REPLAY)
        add_test(NAME forevertas-search-smoke
                 COMMAND forevertas
                     "${FOREVERTAS_TEST_PACK_DIRECTORY}"
                     "${FOREVERTAS_TEST_REPLAY}")
    endif()
endif()
