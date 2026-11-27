/* SPDX-License-Identifier: MIT */
#pragma once
#include <stddef.h>
#include <stdint.h>

/* Exactly one complete DER certificate envelope, or PEM CERTIFICATE block with
 * only ASCII whitespace outside it. Returns decoded bytes, or zero on failure.
 * The caller must also parse X.509 and require a CA certificate before trusting it. */
size_t certificate_file_der(const uint8_t *bytes, size_t length, uint8_t *der, size_t capacity);
