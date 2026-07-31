if(NOT DEFINED GIT_EXECUTABLE OR NOT DEFINED PATCH_FILE OR
   NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "Git executable, patch file, and source directory are required")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE already_applied
    OUTPUT_QUIET
    ERROR_QUIET)
if(already_applied EQUAL 0)
    message(STATUS "Dependency patch already applied: ${PATCH_FILE}")
    return()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE patch_check
    ERROR_VARIABLE patch_error)
if(NOT patch_check EQUAL 0)
    message(FATAL_ERROR "Dependency patch does not apply: ${patch_error}")
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply "${PATCH_FILE}"
    WORKING_DIRECTORY "${SOURCE_DIR}"
    RESULT_VARIABLE patch_result
    ERROR_VARIABLE patch_error)
if(NOT patch_result EQUAL 0)
    message(FATAL_ERROR "Dependency patch failed: ${patch_error}")
endif()
