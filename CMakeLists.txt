cmake_minimum_required(VERSION 3.21)

project(ForeverTAS LANGUAGES CXX)

if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        Debug Release RelWithDebInfo MinSizeRel)
endif()

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
    Quick3D
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
    GIT_SHALLOW FALSE
    PATCH_COMMAND
        ${CMAKE_COMMAND}
            -DDEPENDENCY_SOURCE_DIR=<SOURCE_DIR>
            -DPATCH_FILE=${CMAKE_CURRENT_SOURCE_DIR}/cmake/patches/forevervalidator-race-viewer.patch
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ApplyForeverValidatorPatch.cmake)
FetchContent_MakeAvailable(ForeverValidator)

add_library(forevertas_core STATIC
    src/evaluators/candidate_evaluator.h
    src/evaluators/evaluator_utils.h
    src/evaluators/finish_time_evaluator.cpp
    src/evaluators/finish_time_evaluator.h
    src/evaluators/point_target_evaluator.cpp
    src/evaluators/point_target_evaluator.h
    src/evaluators/pose_target_evaluator.cpp
    src/evaluators/pose_target_evaluator.h
    src/evaluators/velocity_evaluator.cpp
    src/evaluators/velocity_evaluator.h
    src/evaluators/volume_entry_evaluator.cpp
    src/evaluators/volume_entry_evaluator.h
    src/mutations/composite_input_mutator.cpp
    src/mutations/composite_input_mutator.h
    src/mutations/existing_event_perturbation_mutator.cpp
    src/mutations/existing_event_perturbation_mutator.h
    src/mutations/input_deletion_mutator.cpp
    src/mutations/input_deletion_mutator.h
    src/mutations/input_event_formatter.cpp
    src/mutations/input_event_formatter.h
    src/mutations/input_event_utils.cpp
    src/mutations/input_event_utils.h
    src/mutations/input_insertion_mutator.cpp
    src/mutations/input_insertion_mutator.h
    src/mutations/input_mutator.h
    src/mutations/modifier_utils.h
    src/mutations/random_steering_mutator.cpp
    src/mutations/random_steering_mutator.h
    src/mutations/smooth_steering_mutator.cpp
    src/mutations/smooth_steering_mutator.h
    src/searches/algorithm_registry.cpp
    src/searches/algorithm_registry.h
    src/searches/option_configuration.h
    src/searches/option_settings_utils.h
    src/searches/basic_brute_force_search.cpp
    src/searches/basic_brute_force_search.h
    src/searches/search_runner.cpp
    src/searches/search_runner.h)
target_include_directories(forevertas_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(forevertas_core PUBLIC
    ForeverValidator::core
    ForeverValidator::native)

add_library(forevertas_gui STATIC
    src/viewer/race_geometry.cpp
    src/viewer/race_geometry.h
    src/viewer/race_timeline_item.cpp
    src/viewer/race_timeline_item.h
    src/viewer/race_viewer_controller.cpp
    src/viewer/race_viewer_controller.h
    src/app/search_configuration_model.cpp
    src/app/search_configuration_model.h
    src/app/search_completion.h
    src/app/search_controller.cpp
    src/app/search_controller.h
    src/app/packs_directory_finder.cpp
    src/app/packs_directory_finder.h
    src/app/search_worker.cpp
    src/app/search_worker.h)
target_include_directories(forevertas_gui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(forevertas_gui PUBLIC
    forevertas_core
    Qt6::Core
    Qt6::Gui
    Qt6::Quick
    Qt6::Quick3D
    Qt6::Widgets)

qt_add_executable(forevertas
    src/app/main.cpp)
qt_add_qml_module(forevertas
    URI ForeverTAS
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/settings/AlgorithmSelector.qml
        qml/settings/BasicBruteForceSearchSettings.qml
        qml/settings/ConfigurationSection.qml
        qml/settings/ExistingEventPerturbationSettings.qml
        qml/settings/FinishTimeEvaluationSettings.qml
        qml/settings/InputDeletionSettings.qml
        qml/settings/InputInsertionSettings.qml
        qml/settings/ModifierComposition.qml
        qml/settings/PointTargetEvaluationSettings.qml
        qml/settings/PoseTargetEvaluationSettings.qml
        qml/settings/RandomSteeringMutationSettings.qml
        qml/settings/ScrubNumberField.qml
        qml/settings/SettingCombo.qml
        qml/settings/SettingSlider.qml
        qml/settings/SettingSwitch.qml
        qml/settings/SettingTextField.qml
        qml/settings/SmoothSteeringSettings.qml
        qml/settings/StyledComboBox.qml
        qml/settings/TimeWindowSettings.qml
        qml/settings/Vector3Settings.qml
        qml/settings/VelocityEvaluationSettings.qml
        qml/settings/VolumeEntryEvaluationSettings.qml)
target_link_libraries(forevertas PRIVATE
    forevertas_gui
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Quick3D
    Qt6::QuickControls2)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(forevertas_core PRIVATE -Wall -Wextra -Werror)
    target_compile_options(forevertas_gui PRIVATE -Wall -Wextra -Werror)
    target_compile_options(forevertas PRIVATE -Wall -Wextra -Werror)
endif()

if(BUILD_TESTING)
    add_executable(forevertas-strategy-tests
        tests/search_component_tests.cpp)
    target_link_libraries(forevertas-strategy-tests PRIVATE forevertas_core)

    add_executable(forevertas-controller-tests
        tests/search_controller_tests.cpp)
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

        add_executable(forevertas-viewer-smoke
            tests/viewer_smoke.cpp)
        target_link_libraries(forevertas-viewer-smoke PRIVATE
            forevertas_gui
            Qt6::Gui)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(forevertas-viewer-smoke PRIVATE
                -Wall -Wextra -Werror)
        endif()
        add_test(NAME forevertas-viewer
                 COMMAND forevertas-viewer-smoke
                     "${FOREVERTAS_TEST_PACK_DIRECTORY}"
                     "${FOREVERTAS_TEST_REPLAY}")
        set_tests_properties(forevertas-viewer PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            TIMEOUT 180)

        add_executable(forevertas-viewer-qml-smoke
            tests/viewer_qml_smoke.cpp)
        target_compile_definitions(forevertas-viewer-qml-smoke PRIVATE
            FOREVERTAS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(forevertas-viewer-qml-smoke PRIVATE
            forevertas_gui
            Qt6::Gui
            Qt6::Qml
            Qt6::Quick
            Qt6::Quick3D
            Qt6::QuickControls2
            Qt6::Widgets)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
            target_compile_options(forevertas-viewer-qml-smoke PRIVATE
                -Wall -Wextra -Werror)
        endif()
        add_test(NAME forevertas-viewer-qml
                 COMMAND forevertas-viewer-qml-smoke
                     "${FOREVERTAS_TEST_PACK_DIRECTORY}"
                     "${FOREVERTAS_TEST_REPLAY}")
        set_tests_properties(forevertas-viewer-qml PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software"
            TIMEOUT 180)
    endif()
endif()
