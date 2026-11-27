/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
typedef struct mbedtls_x509_crt { int ca_istrue; struct mbedtls_x509_crt *next; struct { unsigned char *p; size_t len; } raw; } mbedtls_x509_crt;
void mbedtls_x509_crt_init(mbedtls_x509_crt *chain);
void mbedtls_x509_crt_free(mbedtls_x509_crt *chain);
int mbedtls_x509_crt_parse(mbedtls_x509_crt *chain, const unsigned char *bytes, size_t length);

int mbedtls_x509_crt_parse_der(mbedtls_x509_crt *chain, const unsigned char *bytes, size_t length);

static inline int mbedtls_x509_crt_get_ca_istrue(const mbedtls_x509_crt *certificate) { return certificate->ca_istrue; }
