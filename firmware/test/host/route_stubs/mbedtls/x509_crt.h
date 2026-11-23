/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
typedef struct { int subject; } mbedtls_x509_crt;
void mbedtls_x509_crt_init(mbedtls_x509_crt *);
void mbedtls_x509_crt_free(mbedtls_x509_crt *);
int mbedtls_x509_crt_parse(mbedtls_x509_crt *, const unsigned char *, size_t);
int mbedtls_x509_dn_gets(char *, size_t, const int *);
