# SPDX-License-Identifier: MIT
include_guard(GLOBAL)

function(perinkpheral_patch_mbedtls_x509 upstream output_dir)
    set(expected_hash cdd67a9f53f3029c712cfd843038ffaf25dc477f9de215f55e585a15bc164995)
    file(SHA256 "${upstream}" actual_hash)
    if(NOT actual_hash STREQUAL expected_hash)
        message(FATAL_ERROR
            "mbedTLS library/x509.c changed: expected ${expected_hash}, got ${actual_hash}. "
            "Review the NumericString patch against this ESP-IDF version; see docs/decisions/0008-moex-tls.md.")
    endif()
    find_package(Git REQUIRED)
    set(patch "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/numericstring.patch")
    file(MAKE_DIRECTORY "${output_dir}/library")
    get_filename_component(output_dir "${output_dir}" REALPATH)
    # Apply only to a disposable build copy, never to the shared ESP-IDF checkout.
    configure_file("${upstream}" "${output_dir}/library/x509.c" COPYONLY)
    # An explicit work tree prevents Git from skipping paths below a repository subdirectory.
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" "--work-tree=${output_dir}" apply --no-index "${patch}"
        WORKING_DIRECTORY "${output_dir}"
        RESULT_VARIABLE patch_result ERROR_VARIABLE patch_error)
    if(NOT patch_result EQUAL 0)
        message(FATAL_ERROR "mbedTLS NumericString patch failed: ${patch_error}")
    endif()
    file(SHA256 "${output_dir}/library/x509.c" patched_hash)
    if(NOT patched_hash STREQUAL "66a9357a43fb9b7ac67eb06a7f921af229cf69566f61cf2c2b32f813c56633a8")
        message(FATAL_ERROR "Unexpected patched mbedTLS x509.c: ${patched_hash}")
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${upstream}" "${patch}")
endfunction()

function(perinkpheral_use_patched_mbedtls upstream output_dir)
    if(NOT TARGET mbedx509)
        message(FATAL_ERROR "ESP-IDF no longer supplies the mbedx509 target; review the NumericString patch integration.")
    endif()
    get_target_property(x509_sources mbedx509 SOURCES)
    # Fail if IDF changes its source layout instead of silently leaving the parser unpatched.
    list(FIND x509_sources x509.c x509_index)
    if(x509_index EQUAL -1)
        message(FATAL_ERROR "mbedx509 no longer compiles x509.c as expected; review the NumericString patch integration.")
    endif()
    perinkpheral_patch_mbedtls_x509("${upstream}" "${output_dir}")
    list(REMOVE_AT x509_sources ${x509_index})
    list(APPEND x509_sources "${output_dir}/library/x509.c")
    set_property(TARGET mbedx509 PROPERTY SOURCES "${x509_sources}")
    message(STATUS "mbedTLS: verified NumericString name-parser patch (decision 0008)")
endfunction()
