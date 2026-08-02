if(NOT DEFINED FOREVERTAS_SOURCE_DIR)
    message(FATAL_ERROR "FOREVERTAS_SOURCE_DIR is required")
endif()

file(GLOB_RECURSE sources
    "${FOREVERTAS_SOURCE_DIR}/src/*.cpp"
    "${FOREVERTAS_SOURCE_DIR}/src/*.h")
set(forbidden
    "\\.durationMs"
    "replay duration")
foreach(source IN LISTS sources)
    file(READ "${source}" contents)
    foreach(pattern IN LISTS forbidden)
        if(contents MATCHES "${pattern}")
            message(FATAL_ERROR
                "Recorded replay duration controls ForeverTAS in ${source}: ${pattern}")
        endif()
    endforeach()
endforeach()
