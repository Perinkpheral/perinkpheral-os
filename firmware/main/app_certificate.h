/* SPDX-License-Identifier: MIT */
#pragma once
#include "certificate_file.h"
#include "mbedtls/x509_crt.h"
#include <stdlib.h>
#include <stdbool.h>

/* Parse into an empty temporary certificate, never directly into the trusted chain. */
static inline bool app_certificate_parse(mbedtls_x509_crt *certificate, const uint8_t *bytes, size_t size)
{
    uint8_t *der = malloc(size);
    if (der == NULL) return false;
    size_t length = certificate_file_der(bytes, size, der, size);
    bool valid = length > 0 && mbedtls_x509_crt_parse_der(certificate, der, length) == 0 &&
                 mbedtls_x509_crt_get_ca_istrue(certificate) == 1 && certificate->next == NULL;
    free(der);
    return valid;
}

/* A downloaded incoming file is checked before package commit. */
bool app_certificate_valid_file(const char *path);
