/* SPDX-License-Identifier: MIT */
#include "certificate_file.h"
#include <stdbool.h>
#include <string.h>

static bool is_space(uint8_t byte)
{
    return byte == ' ' || byte == '\t' || byte == '\r' || byte == '\n';
}

static bool is_complete_der(const uint8_t *bytes, size_t length)
{
    if (length < 2 || bytes[0] != 0x30) return false;
    size_t header = 2, content = bytes[1];
    if (content & 0x80) {
        size_t count = content & 0x7f;
        if (count == 0 || count > sizeof(size_t) || count > length - header || bytes[header] == 0) return false;
        content = 0;
        for (size_t index = 0; index < count; index++) {
            if (content > SIZE_MAX / 256) return false;
            content = content * 256 + bytes[header++];
        }
        if (content < 128) return false;
    }
    return content == length - header;
}

static int base64_digit(uint8_t byte)
{
    if (byte >= 'A' && byte <= 'Z') return byte - 'A';
    if (byte >= 'a' && byte <= 'z') return byte - 'a' + 26;
    if (byte >= '0' && byte <= '9') return byte - '0' + 52;
    return byte == '+' ? 62 : byte == '/' ? 63 : -1;
}

size_t certificate_file_der(const uint8_t *bytes, size_t length, uint8_t *der, size_t capacity)
{
    if (length > 0 && bytes[0] == 0x30) {
        if (length > capacity || !is_complete_der(bytes, length)) return 0;
        memcpy(der, bytes, length);
        return length;
    }
    static const char BEGIN[] = "-----BEGIN CERTIFICATE-----";
    static const char END[] = "-----END CERTIFICATE-----";
    size_t at = 0;
    while (at < length && is_space(bytes[at])) at++;
    if (length - at < sizeof BEGIN - 1 || memcmp(bytes + at, BEGIN, sizeof BEGIN - 1) != 0) return 0;
    at += sizeof BEGIN - 1;
    unsigned bits = 0, accumulator = 0, padding = 0;
    size_t written = 0, digits = 0;
    for (; at < length && bytes[at] != '-'; at++) {
        uint8_t byte = bytes[at];
        if (is_space(byte)) continue;
        if (byte == '=') {
            if (++padding > 2) return 0;
            continue;
        }
        int digit = base64_digit(byte);
        if (digit < 0 || padding != 0) return 0;
        accumulator = (accumulator << 6) | (unsigned)digit;
        digits++;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            if (written == capacity) return 0;
            der[written++] = (uint8_t)(accumulator >> bits);
        }
    }
    if ((digits + padding) % 4 != 0 || bits != padding * 2 ||
        (bits > 0 && (accumulator & ((1u << bits) - 1)) != 0) ||
        length - at < sizeof END - 1 || memcmp(bytes + at, END, sizeof END - 1) != 0) return 0;
    at += sizeof END - 1;
    while (at < length && is_space(bytes[at])) at++;
    return at == length && is_complete_der(der, written) ? written : 0;
}
