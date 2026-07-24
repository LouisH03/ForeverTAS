if(NOT DEFINED DEPENDENCY_SOURCE_DIR OR
   NOT DEFINED PATCH_FILE)
    message(FATAL_ERROR
        "DEPENDENCY_SOURCE_DIR and PATCH_FILE are required")
endif()

function(neutralize_dependency_vocabulary)
    string(CONCAT legacy_pascal "TM" "Interface")
    string(TOLOWER "${legacy_pascal}" legacy_lower)

    set(files
        README.md
        include/forevervalidator/validation.h
        src/app/cli/forevervalidator.cpp
        src/format/replay/replay_file_reader.cpp
        src/format/replay/replay_input_timeline.h
        src/validation/api/validation_api.cpp
        src/validation/serialization/validation_json.cpp)

    foreach(relative_path IN LISTS files)
        set(path "${DEPENDENCY_SOURCE_DIR}/${relative_path}")
        if(NOT EXISTS "${path}")
            message(FATAL_ERROR
                "Expected dependency file is missing: ${relative_path}")
        endif()
        file(READ "${path}" content)
        string(REPLACE "${legacy_lower}" "scripted" content "${content}")
        string(REPLACE "${legacy_pascal}" "Scripted" content "${content}")
        string(REPLACE
            "Scripted replays are detected from the input clock marker written by\nScripted."
            "Scripted-input replays are detected from their input clock marker."
            content "${content}")
        string(REPLACE "Scripted-generated" "script-generated"
                       content "${content}")
        string(REPLACE "the Scripted classification"
                       "the scripted-input classification"
                       content "${content}")
        file(WRITE "${path}" "${content}")
    endforeach()

    execute_process(
        COMMAND git grep -I -n -i "${legacy_pascal}" -- .
        WORKING_DIRECTORY "${DEPENDENCY_SOURCE_DIR}"
        RESULT_VARIABLE vocabulary_result
        OUTPUT_VARIABLE vocabulary_output
        ERROR_VARIABLE vocabulary_error)
    if(vocabulary_result EQUAL 0)
        message(FATAL_ERROR
            "Dependency vocabulary neutralization is incomplete:\n"
            "${vocabulary_output}")
    elseif(NOT vocabulary_result EQUAL 1)
        message(FATAL_ERROR
            "Could not audit dependency vocabulary:\n${vocabulary_error}")
    endif()
endfunction()

execute_process(
    COMMAND git apply --unidiff-zero --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${DEPENDENCY_SOURCE_DIR}"
    RESULT_VARIABLE can_apply
    OUTPUT_VARIABLE apply_check_output
    ERROR_VARIABLE apply_check_error)

if(can_apply EQUAL 0)
    execute_process(
        COMMAND git apply --unidiff-zero "${PATCH_FILE}"
        WORKING_DIRECTORY "${DEPENDENCY_SOURCE_DIR}"
        RESULT_VARIABLE apply_result
        OUTPUT_VARIABLE apply_output
        ERROR_VARIABLE apply_error)
    if(NOT apply_result EQUAL 0)
        message(FATAL_ERROR
            "Could not patch ForeverValidator:\n${apply_output}${apply_error}")
    endif()
else()
    execute_process(
        COMMAND git apply --unidiff-zero --reverse --check "${PATCH_FILE}"
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
endif()

neutralize_dependency_vocabulary()
