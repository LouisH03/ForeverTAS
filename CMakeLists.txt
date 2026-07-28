cmake_minimum_required(VERSION 3.21)

project(ForeverTAS
    VERSION 0.1.0
    DESCRIPTION "Native TrackMania Nations Forever tool-assisted search tool"
    HOMEPAGE_URL "https://github.com/Skycrafter-dev/ForeverTAS"
    LANGUAGES CXX)

option(FOREVERTAS_ENABLE_CUDA
    "Build ForeverTAS with the optional ForeverValidator CUDA backend" OFF)
set(FOREVERTAS_WINDOWS_RUNTIME_DIR "" CACHE PATH
    "Directory containing non-Qt runtime DLLs for Windows packaging")
set(FOREVERTAS_WINDOWS_MSVC_RUNTIME_DIR "" CACHE PATH
    "Directory containing redistributable MSVC runtime DLLs")

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
include(GNUInstallDirs)

set(FOREVERTAS_APP_ID "dev.skycrafter.forevertas")
set(FOREVERTAS_DISPLAY_NAME "ForeverTAS")

find_package(Qt6 6.5 REQUIRED COMPONENTS
    Core
    Gui
    Qml
    Quick
    Quick3D
    QuickControls2
    Widgets)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.7)
    find_package(Qt6 QUIET COMPONENTS GuiPrivate ShaderTools)
endif()
if(BUILD_TESTING)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
endif()
qt_standard_project_setup(REQUIRES 6.5)
qt_policy(SET QTP0004 NEW)

set(FOREVERVALIDATOR_BUILD_CLI OFF CACHE BOOL
    "Do not build the embedded ForeverValidator CLI" FORCE)
set(FOREVERVALIDATOR_BUILD_TESTS OFF CACHE BOOL
    "Do not build embedded ForeverValidator tests" FORCE)
set(FOREVERVALIDATOR_ENABLE_CUDA ${FOREVERTAS_ENABLE_CUDA} CACHE BOOL
    "Enable the embedded ForeverValidator CUDA backend" FORCE)

FetchContent_Declare(
    ForeverValidator
    GIT_REPOSITORY https://github.com/Skycrafter-dev/ForeverValidator.git
    GIT_TAG bbbf87e3611bfecae1599de7381fd9cea4839487
    GIT_SHALLOW FALSE)
FetchContent_MakeAvailable(ForeverValidator)

add_library(forevertas_core STATIC
    src/input_timeline_time.h
    src/physics_backend.h
    src/evaluators/iteration_evaluator.h
    src/evaluators/evaluator_utils.h
    src/evaluators/precise_finish_time_evaluator.cpp
    src/evaluators/precise_finish_time_evaluator.h
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
    src/searches/cuda_batch_calibrator.cpp
    src/searches/cuda_batch_calibrator.h
    src/searches/cuda_search_configuration.cpp
    src/searches/cuda_search_configuration.h
    src/searches/search_runner.cpp
    src/time_format.h
    src/searches/search_runner.h)
target_include_directories(forevertas_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(forevertas_core PUBLIC
    ForeverValidator::core
    ForeverValidator::native)

add_library(forevertas_gui STATIC
    src/viewer/gpu_ray_tracing_view.cpp
    src/viewer/gpu_ray_tracing_view.h
    src/viewer/material_classifier.cpp
    src/viewer/material_classifier.h
    src/viewer/race_geometry.cpp
    src/viewer/race_geometry.h
    src/viewer/ray_tracing_scene.cpp
    src/viewer/ray_tracing_scene.h
    src/viewer/race_timeline_item.cpp
    src/viewer/race_timeline_item.h
    src/viewer/race_viewer_controller.cpp
    src/viewer/race_viewer_controller.h
    src/viewer/visual_scene_pipeline.cpp
    src/viewer/visual_scene_pipeline.h
    src/app/search_configuration_model.cpp
    src/app/search_configuration_model.h
    src/app/search_completion.h
    src/app/search_controller.cpp
    src/app/search_controller.h
    src/app/packs_directory_finder.cpp
    src/app/packs_directory_finder.h
    src/app/panel_wheel_redirector.cpp
    src/app/panel_wheel_redirector.h
    src/app/rolling_throughput.h
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
if(TARGET Qt6::GuiPrivate AND TARGET Qt6::ShaderTools)
    target_compile_definitions(forevertas_gui PUBLIC
        FOREVERTAS_GPU_RAY_TRACING=1)
    target_link_libraries(forevertas_gui PRIVATE Qt6::GuiPrivate)
else()
    target_compile_definitions(forevertas_gui PUBLIC
        FOREVERTAS_GPU_RAY_TRACING=0)
endif()

qt_add_executable(forevertas
    src/app/main.cpp)

set(forevertas_icon_svg
    "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/${FOREVERTAS_APP_ID}.svg")
set_source_files_properties("${forevertas_icon_svg}" PROPERTIES
    QT_RESOURCE_ALIAS "icons/forevertas.svg")
qt_add_resources(forevertas "forevertas_app_resources"
    PREFIX "/"
    FILES "${forevertas_icon_svg}")

if(WIN32)
    configure_file(
        packaging/windows/forevertas.rc.in
        "${CMAKE_CURRENT_BINARY_DIR}/forevertas.rc"
        @ONLY)
    target_sources(forevertas PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/forevertas.rc")
elseif(APPLE)
    set(forevertas_macos_icon
        "${CMAKE_CURRENT_SOURCE_DIR}/packaging/icons/ForeverTAS.icns")
    set_source_files_properties("${forevertas_macos_icon}" PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources")
    target_sources(forevertas PRIVATE "${forevertas_macos_icon}")
endif()

set_target_properties(forevertas PROPERTIES
    OUTPUT_NAME "${FOREVERTAS_DISPLAY_NAME}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bin"
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "${FOREVERTAS_DISPLAY_NAME}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "Copyright (c) 2026 Skycrafter-dev contributors"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${FOREVERTAS_APP_ID}"
    MACOSX_BUNDLE_ICON_FILE "ForeverTAS.icns"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
qt_add_qml_module(forevertas
    URI ForeverTAS
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/settings/AlgorithmSelector.qml
        qml/settings/BasicBruteForceSearchSettings.qml
        qml/settings/ConfigurationSection.qml
        qml/settings/ExistingEventPerturbationSettings.qml
        qml/settings/PreciseFinishTimeEvaluationSettings.qml
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
file(GLOB FOREVERTAS_REPLACEMENT_TEXTURES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/materials/*.png")
qt_add_resources(forevertas "replacement-materials"
    PREFIX "/materials"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/materials"
    FILES ${FOREVERTAS_REPLACEMENT_TEXTURES})
file(GLOB FOREVERTAS_ENVIRONMENT_TEXTURES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/environment/*.png")
qt_add_resources(forevertas "environment-textures"
    PREFIX "/environment"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/environment"
    FILES ${FOREVERTAS_ENVIRONMENT_TEXTURES})
if(TARGET Qt6::GuiPrivate AND TARGET Qt6::ShaderTools)
    qt_add_shaders(forevertas "ray-tracing-shaders"
        PREFIX "/raytracing"
        GLSL "310 es,430"
        FILES
            shaders/raytrace.comp
            shaders/raytrace_present.vert
            shaders/raytrace_present.frag)
endif()
target_link_libraries(forevertas PRIVATE
    forevertas_gui
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::Quick3D
    Qt6::QuickControls2)
target_compile_definitions(forevertas PRIVATE
    FOREVERTAS_VERSION="${PROJECT_VERSION}")

if(APPLE)
    set(forevertas_runtime_destination ".")
elseif(WIN32)
    set(forevertas_runtime_destination ".")
else()
    set(forevertas_runtime_destination "${CMAKE_INSTALL_BINDIR}")
endif()

install(TARGETS forevertas
    BUNDLE DESTINATION .
    RUNTIME DESTINATION "${forevertas_runtime_destination}")

if(WIN32 OR APPLE)
    set(forevertas_deploy_options NO_TRANSLATIONS)
    if(WIN32)
        list(APPEND forevertas_deploy_options NO_COMPILER_RUNTIME)
    else()
        list(APPEND forevertas_deploy_options MACOS_BUNDLE_POST_BUILD)
    endif()
    qt_generate_deploy_qml_app_script(
        TARGET forevertas
        OUTPUT_SCRIPT forevertas_deploy_script
        ${forevertas_deploy_options})
    if(WIN32)
        install(CODE
            "set(QT_DEPLOY_BIN_DIR \".\")\ninclude(\"${forevertas_deploy_script}\")")
    else()
        install(SCRIPT "${forevertas_deploy_script}")
    endif()
endif()

if(WIN32)
    if(NOT FOREVERTAS_WINDOWS_RUNTIME_DIR
            AND DEFINED VCPKG_INSTALLED_DIR
            AND DEFINED VCPKG_TARGET_TRIPLET)
        set(FOREVERTAS_WINDOWS_RUNTIME_DIR
            "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
    endif()
    if(NOT IS_DIRECTORY "${FOREVERTAS_WINDOWS_RUNTIME_DIR}")
        message(FATAL_ERROR
            "FOREVERTAS_WINDOWS_RUNTIME_DIR must point to the vcpkg runtime "
            "directory containing OpenSSL and zlib DLLs")
    endif()
    if(NOT FOREVERTAS_WINDOWS_MSVC_RUNTIME_DIR
            AND DEFINED ENV{VCToolsRedistDir})
        set(FOREVERTAS_WINDOWS_MSVC_RUNTIME_DIR
            "$ENV{VCToolsRedistDir}/x64/Microsoft.VC143.CRT")
    endif()
    if(NOT IS_DIRECTORY "${FOREVERTAS_WINDOWS_MSVC_RUNTIME_DIR}")
        message(FATAL_ERROR
            "FOREVERTAS_WINDOWS_MSVC_RUNTIME_DIR must point to the "
            "Microsoft.VC143.CRT directory")
    endif()

    file(GLOB forevertas_openssl_runtime_dlls
        LIST_DIRECTORIES FALSE
        "${FOREVERTAS_WINDOWS_RUNTIME_DIR}/libcrypto-*.dll")
    file(GLOB forevertas_zlib_runtime_dlls
        LIST_DIRECTORIES FALSE
        "${FOREVERTAS_WINDOWS_RUNTIME_DIR}/z.dll"
        "${FOREVERTAS_WINDOWS_RUNTIME_DIR}/zlib.dll"
        "${FOREVERTAS_WINDOWS_RUNTIME_DIR}/zlib1.dll")
    file(GLOB forevertas_msvc_runtime_dlls
        LIST_DIRECTORIES FALSE
        "${FOREVERTAS_WINDOWS_MSVC_RUNTIME_DIR}/*.dll")
    list(LENGTH forevertas_openssl_runtime_dlls
        forevertas_openssl_runtime_dll_count)
    list(LENGTH forevertas_zlib_runtime_dlls
        forevertas_zlib_runtime_dll_count)
    list(LENGTH forevertas_msvc_runtime_dlls
        forevertas_msvc_runtime_dll_count)
    if(NOT forevertas_openssl_runtime_dll_count EQUAL 1
            OR NOT forevertas_zlib_runtime_dll_count EQUAL 1)
        message(FATAL_ERROR
            "Expected exactly one OpenSSL crypto DLL and one zlib DLL in "
            "${FOREVERTAS_WINDOWS_RUNTIME_DIR}; found "
            "${forevertas_openssl_runtime_dll_count} OpenSSL and "
            "${forevertas_zlib_runtime_dll_count} zlib DLLs")
    endif()
    if(forevertas_msvc_runtime_dll_count EQUAL 0)
        message(FATAL_ERROR
            "No redistributable MSVC runtime DLLs found in "
            "${FOREVERTAS_WINDOWS_MSVC_RUNTIME_DIR}")
    endif()
    install(FILES
        ${forevertas_openssl_runtime_dlls}
        ${forevertas_zlib_runtime_dlls}
        ${forevertas_msvc_runtime_dlls}
        DESTINATION ".")
endif()

if(APPLE)
    set(forevertas_license_destination
        "${FOREVERTAS_DISPLAY_NAME}.app/Contents/Resources/licenses")
elseif(WIN32)
    set(forevertas_license_destination "licenses")
else()
    set(forevertas_license_destination
        "${CMAKE_INSTALL_DATAROOTDIR}/doc/${FOREVERTAS_DISPLAY_NAME}/licenses")
endif()

install(FILES
    LICENSE
    packaging/licenses/LGPL-3.0-only.txt
    packaging/licenses/Qt-GPL-exception-1.0.txt
    packaging/licenses/THIRD_PARTY_NOTICES.md
    DESTINATION "${forevertas_license_destination}")
if(EXISTS "${forevervalidator_SOURCE_DIR}/LICENSE")
    install(FILES "${forevervalidator_SOURCE_DIR}/LICENSE"
        DESTINATION "${forevertas_license_destination}"
        RENAME ForeverValidator-LICENSE.txt)
endif()

if(UNIX AND NOT APPLE)
    install(FILES packaging/linux/${FOREVERTAS_APP_ID}.desktop
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/applications")
    install(FILES packaging/linux/${FOREVERTAS_APP_ID}.metainfo.xml
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/metainfo"
        RENAME ${FOREVERTAS_APP_ID}.appdata.xml)
    install(FILES packaging/icons/${FOREVERTAS_APP_ID}.svg
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps")
    install(FILES packaging/icons/${FOREVERTAS_APP_ID}.png
        DESTINATION "${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/apps")
endif()

set(forevertas_package_arch "${CMAKE_SYSTEM_PROCESSOR}")
if(forevertas_package_arch MATCHES "^(AMD64|amd64|x86_64)$")
    set(forevertas_package_arch "x86_64")
elseif(forevertas_package_arch MATCHES "^(ARM64|arm64|aarch64)$")
    set(forevertas_package_arch "arm64")
endif()

if(WIN32)
    set(forevertas_package_platform "windows")
    set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
    set(forevertas_package_platform "macos")
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "${FOREVERTAS_DISPLAY_NAME} ${PROJECT_VERSION}")
    set(CPACK_DMG_FORMAT "UDZO")
else()
    set(forevertas_package_platform "linux")
    set(CPACK_GENERATOR "TGZ")
endif()

set(CPACK_PACKAGE_NAME "${FOREVERTAS_DISPLAY_NAME}")
set(CPACK_PACKAGE_VENDOR "Skycrafter-dev")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME
    "${FOREVERTAS_DISPLAY_NAME}-${PROJECT_VERSION}-${forevertas_package_platform}-${forevertas_package_arch}")
set(CPACK_PACKAGE_CHECKSUM "SHA256")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packages")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY ON)
include(CPack)

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang"
        AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(FOREVERTAS_WARNING_OPTIONS /W4 /WX)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(FOREVERTAS_WARNING_OPTIONS -Wall -Wextra -Werror)
endif()

if(FOREVERTAS_WARNING_OPTIONS)
    target_compile_options(forevertas_core PRIVATE
        ${FOREVERTAS_WARNING_OPTIONS})
    target_compile_options(forevertas_gui PRIVATE
        ${FOREVERTAS_WARNING_OPTIONS})
    target_compile_options(forevertas PRIVATE
        ${FOREVERTAS_WARNING_OPTIONS})
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

    add_executable(forevertas-renderer-tests
        tests/renderer_tests.cpp)
    target_compile_definitions(forevertas-renderer-tests PRIVATE
        FOREVERTAS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_link_libraries(forevertas-renderer-tests PRIVATE
        forevertas_gui
        Qt6::Gui)

    if(FOREVERTAS_WARNING_OPTIONS)
        target_compile_options(forevertas-strategy-tests PRIVATE
            ${FOREVERTAS_WARNING_OPTIONS})
        target_compile_options(forevertas-controller-tests PRIVATE
            ${FOREVERTAS_WARNING_OPTIONS})
        target_compile_options(forevertas-renderer-tests PRIVATE
            ${FOREVERTAS_WARNING_OPTIONS})
    endif()

    add_test(NAME forevertas-strategies
             COMMAND forevertas-strategy-tests)
    add_test(NAME forevertas-controller
             COMMAND forevertas-controller-tests)
    add_test(NAME forevertas-renderer
             COMMAND forevertas-renderer-tests)
    set_tests_properties(forevertas-renderer PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    add_test(NAME forevertas-qml-smoke
             COMMAND forevertas --qml-smoke-test)
    set_tests_properties(forevertas-qml-smoke PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software")

    add_test(NAME forevertas-install-smoke
        COMMAND "${CMAKE_COMMAND}"
            -DFOREVERTAS_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DFOREVERTAS_STAGE_DIR=${CMAKE_CURRENT_BINARY_DIR}/install-smoke
            -DFOREVERTAS_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/install_smoke.cmake)

    set(FOREVERTAS_TEST_PACK_DIRECTORY "" CACHE PATH
        "Installed TMUF Packs directory used by the optional search smoke test")
    set(FOREVERTAS_TEST_REPLAY "" CACHE FILEPATH
        "Replay used by the optional search smoke test")
    set(FOREVERTAS_TEST_SECOND_REPLAY "" CACHE FILEPATH
        "Distinct replay used by optional viewer reload tests")
    if(FOREVERTAS_TEST_PACK_DIRECTORY AND FOREVERTAS_TEST_REPLAY)
        add_executable(forevertas-search-smoke
            tests/search_smoke.cpp)
        target_link_libraries(forevertas-search-smoke PRIVATE
            forevertas_core)
        if(FOREVERTAS_WARNING_OPTIONS)
            target_compile_options(forevertas-search-smoke PRIVATE
                ${FOREVERTAS_WARNING_OPTIONS})
        endif()
        add_test(NAME forevertas-search-smoke
                 COMMAND forevertas-search-smoke
                     "${FOREVERTAS_TEST_PACK_DIRECTORY}"
                     "${FOREVERTAS_TEST_REPLAY}")
        if(FOREVERTAS_ENABLE_CUDA)
            add_executable(forevertas-cuda-search-parity
                tests/cuda_search_parity_tests.cpp)
            target_link_libraries(forevertas-cuda-search-parity PRIVATE
                forevertas_core)
            if(FOREVERTAS_WARNING_OPTIONS)
                target_compile_options(forevertas-cuda-search-parity PRIVATE
                    ${FOREVERTAS_WARNING_OPTIONS})
            endif()
            add_test(NAME forevertas-cuda-search-parity
                     COMMAND forevertas-cuda-search-parity
                         "${FOREVERTAS_TEST_PACK_DIRECTORY}"
                         "${FOREVERTAS_TEST_REPLAY}")
            set_tests_properties(forevertas-cuda-search-parity PROPERTIES
                LABELS "cuda;parity;performance;stadium"
                RESOURCE_LOCK cuda
                RUN_SERIAL TRUE
                TIMEOUT 240)
            add_test(NAME forevertas-cuda-precise-finish-parity
                     COMMAND forevertas-cuda-search-parity
                         --precise-finish-only
                         "${FOREVERTAS_TEST_PACK_DIRECTORY}"
                         "${FOREVERTAS_TEST_REPLAY}")
            set_tests_properties(
                forevertas-cuda-precise-finish-parity PROPERTIES
                LABELS "cuda;parity;precise-finish;stadium"
                RESOURCE_LOCK cuda
                RUN_SERIAL TRUE
                TIMEOUT 300)
            add_test(NAME forevertas-cuda-calibration
                     COMMAND forevertas-cuda-search-parity
                         --calibration-only
                         "${FOREVERTAS_TEST_PACK_DIRECTORY}"
                         "${FOREVERTAS_TEST_REPLAY}")
            set_tests_properties(forevertas-cuda-calibration PROPERTIES
                LABELS "cuda;calibration;parity;stadium"
                RESOURCE_LOCK cuda
                TIMEOUT 120)
        endif()
        add_test(NAME forevertas-controller-lifecycle
                 COMMAND forevertas-controller-tests
                     --lifecycle
                     "${FOREVERTAS_TEST_PACK_DIRECTORY}"
                     "${FOREVERTAS_TEST_REPLAY}")

        add_executable(forevertas-render-scene-data-smoke
            tests/render_scene_data_smoke.cpp)
        target_link_libraries(forevertas-render-scene-data-smoke PRIVATE
            forevertas_core)
        if(FOREVERTAS_WARNING_OPTIONS)
            target_compile_options(forevertas-render-scene-data-smoke PRIVATE
                ${FOREVERTAS_WARNING_OPTIONS})
        endif()
        add_test(NAME forevertas-render-scene-data
                 COMMAND forevertas-render-scene-data-smoke
                     "${FOREVERTAS_TEST_PACK_DIRECTORY}"
                     "${FOREVERTAS_TEST_REPLAY}")
        set_tests_properties(forevertas-render-scene-data PROPERTIES
            TIMEOUT 180)

        add_executable(forevertas-viewer-reload-smoke
            tests/viewer_reload_smoke.cpp)
        target_link_libraries(forevertas-viewer-reload-smoke PRIVATE
            forevertas_gui
            Qt6::Gui)
        if(FOREVERTAS_WARNING_OPTIONS)
            target_compile_options(forevertas-viewer-reload-smoke PRIVATE
                ${FOREVERTAS_WARNING_OPTIONS})
        endif()
        add_test(NAME forevertas-viewer-reload-same
                 COMMAND forevertas-viewer-reload-smoke
                     "${FOREVERTAS_TEST_PACK_DIRECTORY}"
                     "${FOREVERTAS_TEST_REPLAY}"
                     "${FOREVERTAS_TEST_REPLAY}")
        set_tests_properties(forevertas-viewer-reload-same PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            TIMEOUT 180)

        if(FOREVERTAS_TEST_SECOND_REPLAY)
            add_test(NAME forevertas-viewer-reload
                     COMMAND forevertas-viewer-reload-smoke
                         "${FOREVERTAS_TEST_PACK_DIRECTORY}"
                         "${FOREVERTAS_TEST_REPLAY}"
                         "${FOREVERTAS_TEST_SECOND_REPLAY}")
            set_tests_properties(forevertas-viewer-reload PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                TIMEOUT 180)

        endif()

        add_executable(forevertas-viewer-smoke
            tests/viewer_smoke.cpp)
        target_link_libraries(forevertas-viewer-smoke PRIVATE
            forevertas_gui
            Qt6::Gui)
        if(FOREVERTAS_WARNING_OPTIONS)
            target_compile_options(forevertas-viewer-smoke PRIVATE
                ${FOREVERTAS_WARNING_OPTIONS})
        endif()
        add_test(NAME forevertas-viewer
                 COMMAND forevertas-viewer-smoke
                     "${FOREVERTAS_TEST_PACK_DIRECTORY}"
                     "${FOREVERTAS_TEST_REPLAY}")
        set_tests_properties(forevertas-viewer PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            TIMEOUT 180)

        if(FOREVERTAS_TEST_SECOND_REPLAY)
            add_executable(forevertas-viewer-reload-qml-smoke
                tests/viewer_reload_qml_smoke.cpp)
            target_compile_definitions(forevertas-viewer-reload-qml-smoke PRIVATE
                FOREVERTAS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
            target_link_libraries(forevertas-viewer-reload-qml-smoke PRIVATE
                forevertas_gui
                Qt6::Qml
                Qt6::Quick
                Qt6::QuickControls2
                Qt6::Widgets)
            if(FOREVERTAS_WARNING_OPTIONS)
                target_compile_options(forevertas-viewer-reload-qml-smoke PRIVATE
                    ${FOREVERTAS_WARNING_OPTIONS})
            endif()
            add_test(NAME forevertas-viewer-reload-qml
                     COMMAND forevertas-viewer-reload-qml-smoke
                         "${FOREVERTAS_TEST_PACK_DIRECTORY}"
                         "${FOREVERTAS_TEST_REPLAY}"
                         "${FOREVERTAS_TEST_SECOND_REPLAY}")
            set_tests_properties(forevertas-viewer-reload-qml PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software"
                TIMEOUT 180)

        endif()

        add_executable(forevertas-viewer-qml-smoke
            tests/viewer_qml_smoke.cpp)
        qt_add_resources(forevertas-viewer-qml-smoke
            "replacement-materials-test"
            PREFIX "/materials"
            BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/materials"
            FILES ${FOREVERTAS_REPLACEMENT_TEXTURES})
        qt_add_resources(forevertas-viewer-qml-smoke
            "environment-textures-test"
            PREFIX "/environment"
            BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/environment"
            FILES ${FOREVERTAS_ENVIRONMENT_TEXTURES})
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
        if(FOREVERTAS_WARNING_OPTIONS)
            target_compile_options(forevertas-viewer-qml-smoke PRIVATE
                ${FOREVERTAS_WARNING_OPTIONS})
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
