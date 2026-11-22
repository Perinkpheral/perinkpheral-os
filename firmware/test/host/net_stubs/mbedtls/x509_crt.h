/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
typedef struct { int unused; } mbedtls_x509_crt;
void mbedtls_x509_crt_init(mbedtls_x509_crt *chain);
void mbedtls_x509_crt_free(mbedtls_x509_crt *chain);
int mbedtls_x509_crt_parse(mbedtls_x509_crt *chain, const unsigned char *bytes, size_t length);
