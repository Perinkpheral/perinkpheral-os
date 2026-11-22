/* SPDX-License-Identifier: MIT */
#pragma once
#include "x509_crt.h"
typedef struct { int unused; } mbedtls_ssl_config;
void mbedtls_ssl_conf_ca_chain(mbedtls_ssl_config *config, mbedtls_x509_crt *chain, void *revocations);
