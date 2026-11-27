/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
typedef struct mbedtls_x509_crt { int subject, ca_istrue; struct mbedtls_x509_crt *next; } mbedtls_x509_crt;
void mbedtls_x509_crt_init(mbedtls_x509_crt *);
void mbedtls_x509_crt_free(mbedtls_x509_crt *);
int mbedtls_x509_crt_parse_der(mbedtls_x509_crt *, const unsigned char *, size_t);
int mbedtls_x509_dn_gets(char *, size_t, const int *);

static inline int mbedtls_x509_crt_get_ca_istrue(const mbedtls_x509_crt *certificate) { return certificate->ca_istrue; }
