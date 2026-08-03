if(NOT DEFINED FOREVERTAS_SOURCE_DIR)
    message(FATAL_ERROR "FOREVERTAS_SOURCE_DIR is required")
endif()

file(READ "${FOREVERTAS_SOURCE_DIR}/src/app/search_controller.cpp" controller)
if(controller MATCHES "QFileDialog|getOpenFileName|getExistingDirectory")
    message(FATAL_ERROR
        "Browse actions must not use Qt's file dialog implementation")
endif()
if(NOT controller MATCHES "OpenSystemDirectoryDialog" OR
   NOT controller MATCHES "OpenSystemFileDialog")
    message(FATAL_ERROR "Browse actions do not use the system dialog service")
endif()

file(READ "${FOREVERTAS_SOURCE_DIR}/src/app/system_file_dialog.cpp" implementation)
if(NOT implementation MATCHES "org.freedesktop.portal.FileChooser" OR
   NOT implementation MATCHES "CLSID_FileOpenDialog")
    message(FATAL_ERROR
        "Linux and Windows system dialog implementations are both required")
endif()
