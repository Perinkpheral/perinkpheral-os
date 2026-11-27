/* SPDX-License-Identifier: MIT */
#include "certificate_file.h"
#include "tinytest.h"
#include <string.h>

int main(void)
{
    uint8_t der[128];
    static const uint8_t sequence[] = {0x30, 3, 1, 2, 3};
    CHECK(certificate_file_der(sequence, sizeof sequence, der, sizeof der) == sizeof sequence);
    CHECK(memcmp(sequence, der, sizeof sequence) == 0);
    uint8_t doubled[] = {0x30, 3, 1, 2, 3, 0x30, 3, 1, 2, 3};
    CHECK(certificate_file_der(doubled, sizeof doubled, der, sizeof der) == 0);
    CHECK(certificate_file_der(sequence, sizeof sequence - 1, der, sizeof der) == 0);
    static const char pem[] = " \n-----BEGIN CERTIFICATE-----\nMAMBAgM=\n-----END CERTIFICATE-----\n";
    CHECK(certificate_file_der((const uint8_t *)pem, strlen(pem), der, sizeof der) == sizeof sequence);
    CHECK(memcmp(sequence, der, sizeof sequence) == 0);
    char extra[2 * sizeof pem];
    snprintf(extra, sizeof extra, "%s%s", pem, pem);
    CHECK(certificate_file_der((const uint8_t *)extra, strlen(extra), der, sizeof der) == 0);
    snprintf(extra, sizeof extra, "%sgarbage", pem);
    CHECK(certificate_file_der((const uint8_t *)extra, strlen(extra), der, sizeof der) == 0);
    static const char partial[] = "-----BEGIN CERTIFICATE-----\nMAMBAgM=\n";
    CHECK(certificate_file_der((const uint8_t *)partial, strlen(partial), der, sizeof der) == 0);
    REPORT();
}
