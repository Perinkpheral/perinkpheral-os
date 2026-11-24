# SPDX-License-Identifier: MIT
if(CHILD)
    include("${PATCH_HELPER}")
    perinkpheral_patch_mbedtls_x509("${CHANGED_SOURCE}" "${OUTPUT_DIR}")
else()
    file(READ "${UPSTREAM}" original)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/changed-x509.c" "${original}\n")
    execute_process(COMMAND "${CMAKE_COMMAND}"
        -DCHILD=ON "-DPATCH_HELPER=${PATCH_HELPER}"
        "-DCHANGED_SOURCE=${CMAKE_CURRENT_BINARY_DIR}/changed-x509.c"
        "-DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/guard-output"
        -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if(result EQUAL 0 OR NOT error MATCHES "mbedTLS library/x509.c changed")
        message(FATAL_ERROR "Changed upstream was not refused with a useful diagnostic: ${error}")
    endif()
endif()
