if(NOT DEFINED DEPENDENCY_SOURCE_DIR OR
   NOT DEFINED PATCH_FILE)
    message(FATAL_ERROR
        "DEPENDENCY_SOURCE_DIR and PATCH_FILE are required")
endif()

execute_process(
    COMMAND git apply --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${DEPENDENCY_SOURCE_DIR}"
    RESULT_VARIABLE can_apply
    OUTPUT_VARIABLE apply_check_output
    ERROR_VARIABLE apply_check_error)

if(can_apply EQUAL 0)
    execute_process(
        COMMAND git apply "${PATCH_FILE}"
        WORKING_DIRECTORY "${DEPENDENCY_SOURCE_DIR}"
        RESULT_VARIABLE apply_result
        OUTPUT_VARIABLE apply_output
        ERROR_VARIABLE apply_error)
    if(NOT apply_result EQUAL 0)
        message(FATAL_ERROR
            "Could not patch ForeverValidator:\n${apply_output}${apply_error}")
    endif()
    return()
endif()

execute_process(
    COMMAND git apply --reverse --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${DEPENDENCY_SOURCE_DIR}"
    RESULT_VARIABLE already_applied
    OUTPUT_VARIABLE reverse_check_output
    ERROR_VARIABLE reverse_check_error)

if(NOT already_applied EQUAL 0)
    message(FATAL_ERROR
        "ForeverValidator patch is neither applicable nor already applied.\n"
        "Apply check:\n${apply_check_output}${apply_check_error}\n"
        "Reverse check:\n${reverse_check_output}${reverse_check_error}")
endif()
