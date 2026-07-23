cmake_minimum_required(VERSION 3.21)

project(ForeverTAS LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
include(CTest)

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    QuickControls2
    Widgets)
if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
endif()
qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

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
    src/searches/serial_brute_force_search.cpp
    src/searches/serial_search_runner.cpp)
target_include_directories(forevertas_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(forevertas_core PUBLIC
    ForeverValidator::core
    ForeverValidator::native)

add_library(forevertas_gui STATIC
    src/app/search_controller.cpp
    src/app/search_controller.h
    src/app/search_worker.cpp
    src/app/search_worker.h)
target_include_directories(forevertas_gui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(forevertas_gui PUBLIC
    forevertas_core
    Qt6::Core
    Qt6::Widgets)

qt_add_executable(forevertas
    src/app/main.cpp)
qt_add_qml_module(forevertas
    URI ForeverTAS
    VERSION 1.0
    QML_FILES
        qml/Main.qml)
target_link_libraries(forevertas PRIVATE
    forevertas_gui
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(forevertas_core PRIVATE -Wall -Wextra -Werror)
    target_compile_options(forevertas_gui PRIVATE -Wall -Wextra -Werror)
    target_compile_options(forevertas PRIVATE -Wall -Wextra -Werror)
endif()

if(BUILD_TESTING)
    add_executable(forevertas-strategy-tests tests/strategy_tests.cpp)
    target_link_libraries(forevertas-strategy-tests PRIVATE forevertas_core)

    add_executable(forevertas-controller-tests
        tests/controller_tests.cpp)
    target_link_libraries(forevertas-controller-tests PRIVATE
        forevertas_gui
        Qt6::Test)

    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(forevertas-strategy-tests PRIVATE
            -Wall -Wextra -Werror)
        target_compile_options(forevertas-controller-tests PRIVATE
            -Wall -Wextra -Werror)
    endif()

    add_test(NAME forevertas-strategies
             COMMAND forevertas-strategy-tests)
    add_test(NAME forevertas-controller
             COMMAND forevertas-controller-tests)
    add_test(NAME forevertas-qml-smoke
             COMMAND forevertas --qml-smoke-test)
    set_tests_properties(forevertas-qml-smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software")

    set(FOREVERTAS_TEST_PACK_DIRECTORY "" CACHE PATH
        "Installed TMUF Packs directory used by the optional search smoke test")
    set(FOREVERTAS_TEST_REPLAY "" CACHE FILEPATH
        "Replay used by the optional search smoke test")
    if(FOREVERTAS_TEST_PACK_DIRECTORY AND FOREVERTAS_TEST_REPLAY)
        add_executable(forevertas-search-smoke
            tests/search_smoke.cpp)
        target_link_libraries(forevertas-search-smoke PRIVATE
            forevertas_core)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(forevertas-search-smoke PRIVATE
                -Wall -Wextra -Werror)
        endif()
        add_test(NAME forevertas-search-smoke
                 COMMAND forevertas-search-smoke
                     "${FOREVERTAS_TEST_PACK_DIRECTORY}"
                     "${FOREVERTAS_TEST_REPLAY}")
    endif()
endif()
