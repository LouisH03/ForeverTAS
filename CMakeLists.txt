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

add_executable(forevertas src/main.cpp)
target_link_libraries(forevertas PRIVATE
    ForeverValidator::native
    ForeverValidator::core)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(forevertas PRIVATE
        -Wall
        -Wextra
        -Werror)
endif()

include(CTest)
if(BUILD_TESTING)
    add_test(NAME forevertas-help
             COMMAND forevertas --help)
endif()
