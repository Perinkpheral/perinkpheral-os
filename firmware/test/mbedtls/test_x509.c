// SPDX-License-Identifier: MIT
#include "common.h"
#include "mbedtls/asn1.h"
#include "mbedtls/x509_crt.h"
#include "x509_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { numeric_string_tag = 0x12 };

static int test_name_tags(void)
{
    const unsigned char accepted_tags[] = {
        MBEDTLS_ASN1_BMP_STRING, MBEDTLS_ASN1_UTF8_STRING,
        MBEDTLS_ASN1_T61_STRING, MBEDTLS_ASN1_PRINTABLE_STRING,
        MBEDTLS_ASN1_IA5_STRING, MBEDTLS_ASN1_UNIVERSAL_STRING,
        MBEDTLS_ASN1_BIT_STRING, numeric_string_tag
    };
    for (size_t index = 0; index < sizeof(accepted_tags); ++index) {
        unsigned char encoded_name[] = { 0x31, 0x0a, 0x30, 0x08, 0x06, 0x02, 0x2a, 0x03, 0x00, 0x02, '1', '2' };
        encoded_name[8] = accepted_tags[index];
        unsigned char *cursor = encoded_name;
        mbedtls_x509_name name = { 0 };
        int result = mbedtls_x509_get_name(&cursor, encoded_name + sizeof(encoded_name), &name);
        int expected = 0;
#ifdef EXPECT_UNPATCHED
        if (accepted_tags[index] == numeric_string_tag) {
            expected = MBEDTLS_ERR_X509_INVALID_NAME + MBEDTLS_ERR_ASN1_UNEXPECTED_TAG;
        }
#endif
        if (result != expected || (result == 0 &&
            (cursor != encoded_name + sizeof(encoded_name) || name.val.tag != accepted_tags[index] ||
             name.val.len != 2 || memcmp(name.val.p, "12", 2) != 0))) {
            fprintf(stderr, "Unexpected name parser result for tag 0x%02x: %d\n", accepted_tags[index], result);
            return 1;
        }
    }
    unsigned char invalid_name[] = { 0x31, 0x0a, 0x30, 0x08, 0x06, 0x02, 0x2a, 0x03, 0x01, 0x02, '1', '2' };
    unsigned char *cursor = invalid_name;
    mbedtls_x509_name name = { 0 };
    if (mbedtls_x509_get_name(&cursor, invalid_name + sizeof(invalid_name), &name) !=
        MBEDTLS_ERR_X509_INVALID_NAME + MBEDTLS_ERR_ASN1_UNEXPECTED_TAG) {
        return 1;
    }
    invalid_name[8] = MBEDTLS_ASN1_PRINTABLE_STRING;
    invalid_name[9] = 3; /* Value runs beyond its enclosing sequence. */
    cursor = invalid_name;
    return mbedtls_x509_get_name(&cursor, invalid_name + sizeof(invalid_name), &name) == 0;
}

static int test_numericstring_alphabet(void)
{
    for (unsigned value = 0; value <= 255; ++value) {
        for (size_t position = 10; position < 12; ++position) {
            unsigned char encoded_name[] = { 0x31, 0x0a, 0x30, 0x08, 0x06, 0x02, 0x2a, 0x03, 0x12, 0x02, '0', ' ' };
            encoded_name[position] = (unsigned char)value;
            unsigned char *cursor = encoded_name;
            mbedtls_x509_name name = { 0 };
            int result = mbedtls_x509_get_name(&cursor, encoded_name + sizeof(encoded_name), &name);
#ifdef EXPECT_UNPATCHED
            int expected = MBEDTLS_ERR_X509_INVALID_NAME + MBEDTLS_ERR_ASN1_UNEXPECTED_TAG;
#else
            int expected = (value == ' ' || (value >= '0' && value <= '9')) ? 0 :
                MBEDTLS_ERR_X509_INVALID_NAME + MBEDTLS_ERR_ASN1_INVALID_DATA;
#endif
            if (result != expected) {
                fprintf(stderr, "Unexpected NumericString byte 0x%02x at %zu: %d\n", value, position, result);
                return 1;
            }
        }
    }
    return 0;
}

static int test_numericstring_wildcard(const char *certificate_path, const char *root_path)
{
    mbedtls_x509_crt certificate;
    mbedtls_x509_crt root;
    mbedtls_x509_crt_init(&certificate);
    mbedtls_x509_crt_init(&root);
    int failed = mbedtls_x509_crt_parse_file(&root, root_path) != 0;
    int result = mbedtls_x509_crt_parse_file(&certificate, certificate_path);
#ifdef EXPECT_UNPATCHED
    int expected = MBEDTLS_ERR_X509_INVALID_NAME + MBEDTLS_ERR_ASN1_UNEXPECTED_TAG;
#else
    int expected = MBEDTLS_ERR_X509_INVALID_NAME + MBEDTLS_ERR_ASN1_INVALID_DATA;
#endif
    if (result != expected) {
        fprintf(stderr, "Unexpected NumericString wildcard certificate parse: %d\n", result);
        if (result == 0) {
            uint32_t flags = 0;
            int verified = mbedtls_x509_crt_verify(&certificate, &root, NULL,
                                                 "service.example.com", &flags, NULL, NULL);
            fprintf(stderr, "Wildcard CN verification: %d; flags: 0x%08x; SAN present: %d\n",
                    verified, (unsigned)flags, certificate.subject_alt_names.buf.p != NULL);
        }
        failed = 1;
    }
    mbedtls_x509_crt_free(&root);
    mbedtls_x509_crt_free(&certificate);
    return failed;
}

int main(int argc, char **argv)
{
    if (argc != 4) {
        return 1;
    }
    mbedtls_x509_crt certificate;
    mbedtls_x509_crt_init(&certificate);
    int result = mbedtls_x509_crt_parse_file(&certificate, argv[1]);
#ifdef EXPECT_UNPATCHED
    int failed = result != MBEDTLS_ERR_X509_INVALID_NAME + MBEDTLS_ERR_ASN1_UNEXPECTED_TAG;
    printf("Original MOEX leaf parse: -0x%04x (expected -0x23e2)\n", -result);
#else
    int failed = result != 0;
    size_t numeric_attributes = 0;
    for (const mbedtls_x509_name *name = &certificate.subject; result == 0 && name != NULL; name = name->next) {
        if (name->val.tag != numeric_string_tag) {
            continue;
        }
        const char *expected_value = NULL;
        if (name->oid.len == 5 && memcmp(name->oid.p, "\x2a\x85\x03\x64\x04", 5) == 0) {
            expected_value = "7702077840";
        } else if (name->oid.len == 5 && memcmp(name->oid.p, "\x2a\x85\x03\x64\x01", 5) == 0) {
            expected_value = "1027739387411";
        }
        if (expected_value == NULL || name->val.len != strlen(expected_value) ||
            memcmp(name->val.p, expected_value, name->val.len) != 0) {
            failed = 1;
        }
        ++numeric_attributes;
    }
    failed |= numeric_attributes != 2;
    printf("Patched MOEX leaf parse: %d; NumericString INN/OGRN attributes: %zu\n", result, numeric_attributes);
#endif
    mbedtls_x509_crt_free(&certificate);
    return failed | test_name_tags() | test_numericstring_alphabet() |
           test_numericstring_wildcard(argv[2], argv[3]);
}
