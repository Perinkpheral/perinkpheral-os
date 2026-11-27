/* SPDX-License-Identifier: MIT */
#include "app_certificate.h"
#include "app_manifest.h"
#include <stdio.h>

bool app_certificate_valid_file(const char *path)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) return false;
    uint8_t *bytes = malloc(MANIFEST_CERTIFICATE_MAX_BYTES + 1);
    if (bytes == NULL) { fclose(file); return false; }
    size_t length = fread(bytes, 1, MANIFEST_CERTIFICATE_MAX_BYTES + 1, file);
    bool complete = !ferror(file) && feof(file) && length > 0 && length <= MANIFEST_CERTIFICATE_MAX_BYTES;
    fclose(file);
    mbedtls_x509_crt certificate;
    mbedtls_x509_crt_init(&certificate);
    bool valid = complete && app_certificate_parse(&certificate, bytes, length);
    mbedtls_x509_crt_free(&certificate);
    free(bytes);
    return valid;
}
