# Native mbedTLS regression and served-chain verification

These tests use the same ESP-IDF mbedTLS sources and verified `x509.c` patch as
main firmware, with upstream's native default configuration. They need a C11
compiler, CMake, Git and Python, but no ESP cross compiler. From the repository
root, set `IDF_PATH` to ESP-IDF 5.5.5, then:

```sh
cmake -S firmware/test/mbedtls -B /tmp/mbedtls-host
cmake --build /tmp/mbedtls-host --parallel 2
ctest --test-dir /tmp/mbedtls-host --output-on-failure
```

Alternatively, pass `-DMBEDTLS_SOURCE=<idf>/components/mbedtls/mbedtls` to CMake.
Do not configure this native project with IDF's cross-compilation toolchain.
CI runs it in each firmware job inside `espressif/idf:v5.5.5`, where the exact
sources used by the firmware already exist. The ordinary pure-module host test
suite continues to work without ESP-IDF.

The offline tests need no network or CA installation. They check the saved
public MOEX leaf and its two NumericString OID/value pairs, every previously
accepted name tag, rejection of an unrelated tag and of a truncated value,
and refusal of a changed upstream source. A second executable uses the original
`x509.c` object with the same crypto library and reproduces exactly -0x23E2.
The positive certificate test would fail against that parser. Expiration does
not affect certificate parsing.

For a live end-to-end certificate check, use the root from Stocks' prepared
catalog change. Set `PERINKPHERAL_APPS` to a local catalog checkout containing
commit `9b2932916e0c6009d0b9be02d0a66e21dcacf059` (`fix/moex-ca` when tested):

```sh
git -C "$PERINKPHERAL_APPS" show \
  9b2932916e0c6009d0b9be02d0a66e21dcacf059:apps/stocks/files/russian-root-ca.pem \
  > /tmp/stocks-root.pem
sh firmware/test/mbedtls/verify-moex.sh \
  /tmp/mbedtls-host /tmp/stocks-root.pem /tmp/moex-verification
```

The script uses OpenSSL only to fetch the complete served chain with SNI. It
does not use OpenSSL's verification result as evidence. `verify_chain`, linked
to the patched mbedTLS, parses every served certificate without skipping
failures and calls `mbedtls_x509_crt_verify()` against only Stocks' root, with
the hostname `iss.moex.com`. It requires both return code and flags to be zero;
it then requires verification of `wrong.example` to fail. Certificate validity
uses the computer's current clock. Outputs stay outside the repository.

On 2026-11-24 the server returned four certificates (including a duplicate
leaf). Patched verification returned 0, flags `0x00000000`; the wrong hostname
failed with flags `0x00000004`. The same chain against IDF's Mozilla bundle
instead of Stocks' root failed with flags `0x00000008` (not trusted):

```sh
/tmp/mbedtls-host/verify_chain /tmp/moex-verification/served-chain.pem \
  "$IDF_PATH/components/mbedtls/esp_crt_bundle/cacrt_all.pem" iss.moex.com
```

This verifies the served certificate chain, trust and hostname in native
mbedTLS, not an ESP hardware TLS handshake. No device is contacted. A later
server certificate or trust-chain change can legitimately change live results;
the committed leaf keeps the parser regression reproducible offline.
