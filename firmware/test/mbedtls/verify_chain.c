// SPDX-License-Identifier: MIT
#include "mbedtls/x509_crt.h"

#include <stdint.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "Usage: verify_chain served-chain.pem trusted-root.pem hostname\n");
        return 1;
    }
    mbedtls_x509_crt chain;
    mbedtls_x509_crt root;
    mbedtls_x509_crt_init(&chain);
    mbedtls_x509_crt_init(&root);
    uint32_t flags = 0;
    int result = mbedtls_x509_crt_parse_file(&chain, argv[1]);
    if (result != 0) {
        fprintf(stderr, "Chain parse failed: %d\n", result);
        goto done;
    }
    size_t certificates = 0;
    for (const mbedtls_x509_crt *certificate = &chain; certificate != NULL; certificate = certificate->next) {
        ++certificates;
    }
    result = mbedtls_x509_crt_parse_file(&root, argv[2]);
    if (result != 0) {
        fprintf(stderr, "Root parse failed: %d\n", result);
        goto done;
    }
    result = mbedtls_x509_crt_verify(&chain, &root, NULL, argv[3], &flags, NULL, NULL);
    printf("Chain certificates: %zu; hostname: %s; verification: %d; flags: 0x%08x\n",
           certificates, argv[3], result, (unsigned)flags);
    if (flags != 0) {
        char reason[1024];
        mbedtls_x509_crt_verify_info(reason, sizeof(reason), "  ", flags);
        fputs(reason, stderr);
    }
done:
    mbedtls_x509_crt_free(&root);
    mbedtls_x509_crt_free(&chain);
    return result != 0 || flags != 0;
}
