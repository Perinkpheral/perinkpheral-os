# SPDX-License-Identifier: MIT
cmake_minimum_required(VERSION 3.17)
project(perinkpheral_x509_test C)
enable_testing()
set(CMAKE_C_STANDARD 11)
if(NOT MBEDTLS_SOURCE)
    set(MBEDTLS_SOURCE "$ENV{IDF_PATH}/components/mbedtls/mbedtls")
endif()
if(NOT EXISTS "${MBEDTLS_SOURCE}/library/x509.c")
    message(FATAL_ERROR "Set IDF_PATH or MBEDTLS_SOURCE to ESP-IDF 5.5.5's mbedTLS sources.")
endif()
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(GEN_FILES OFF CACHE BOOL "" FORCE)
add_subdirectory("${MBEDTLS_SOURCE}" mbedtls EXCLUDE_FROM_ALL)
include("../../patches/mbedtls/patch.cmake")
perinkpheral_use_patched_mbedtls("${MBEDTLS_SOURCE}/library/x509.c" "${CMAKE_BINARY_DIR}/patched-mbedtls")
add_test(NAME changed_upstream_is_refused COMMAND "${CMAKE_COMMAND}"
    "-DUPSTREAM=${MBEDTLS_SOURCE}/library/x509.c"
    "-DPATCH_HELPER=${CMAKE_CURRENT_SOURCE_DIR}/../../patches/mbedtls/patch.cmake"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/test_patch_guard.cmake")
add_test(NAME separate_build_directories COMMAND "${CMAKE_COMMAND}"
    "-DUPSTREAM=${MBEDTLS_SOURCE}/library/x509.c"
    "-DPATCH_HELPER=${CMAKE_CURRENT_SOURCE_DIR}/../../patches/mbedtls/patch.cmake"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/test_patch_build_dirs.cmake")

add_executable(test_x509 test_x509.c)
target_link_libraries(test_x509 PRIVATE mbedx509)
target_include_directories(test_x509 PRIVATE "${MBEDTLS_SOURCE}/library")
target_compile_options(test_x509 PRIVATE -Wall -Wextra -Werror)
set(certificate_fixtures
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/iss-moex-com.pem"
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/numericstring-wildcard.pem"
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/test-root.pem")
add_test(NAME numericstring COMMAND test_x509 ${certificate_fixtures})

# The original object satisfies x509.c's symbols before the patched archive is searched.
add_executable(test_x509_unpatched test_x509.c "${MBEDTLS_SOURCE}/library/x509.c")
target_link_libraries(test_x509_unpatched PRIVATE mbedx509)
target_include_directories(test_x509_unpatched PRIVATE "${MBEDTLS_SOURCE}/library")
target_compile_definitions(test_x509_unpatched PRIVATE EXPECT_UNPATCHED=1)
add_test(NAME original_rejects_numericstring COMMAND test_x509_unpatched ${certificate_fixtures})

add_executable(verify_chain verify_chain.c)
target_link_libraries(verify_chain PRIVATE mbedx509)
target_compile_options(verify_chain PRIVATE -Wall -Wextra -Werror)
