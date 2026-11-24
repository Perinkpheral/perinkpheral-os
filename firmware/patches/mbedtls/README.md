# NumericString X.509 names

Decision [0008](../../../docs/decisions/0008-moex-tls.md) authorizes this parser
correction. `numericstring.patch` changes only `library/x509.c`: name values
accept tag 0x12, alongside the existing accepted tags, and validate every
NumericString byte against X.680's alphabet (ASCII digits 0-9 and space).
Invalid content returns `MBEDTLS_ERR_X509_INVALID_NAME` plus
`MBEDTLS_ERR_ASN1_INVALID_DATA` before the name can reach hostname matching.
The private `MBEDTLS_ASN1_NUMERIC_STRING` definition is needed because the bundled ASN.1
header does not name it. Other tags and verification behavior are unchanged.
The generated file retains upstream's Apache-2.0 OR GPL-2.0-or-later header.

Base: ESP-IDF v5.5.5, Espressif mbedTLS 3.6.6, commit
`9d669eadb1955d348986b9280156710aaaadf79f`.

- Original `library/x509.c` SHA-256:
  `cdd67a9f53f3029c712cfd843038ffaf25dc477f9de215f55e585a15bc164995`.
- Patched SHA-256:
  `66a9357a43fb9b7ac67eb06a7f921af229cf69566f61cf2c2b32f813c56633a8`.

After `project()`, main firmware uses `patch.cmake` to copy that verified source
to `<build>/patched-mbedtls/library/x509.c`, apply the diff with Git and replace
only the `mbedx509` target's `x509.c` source. Git and CMake are already required
by ESP-IDF. The target keeps upstream's include paths, compile definitions and
Espressif port settings. Original and patched hashes, patch application and the
expected target/source layout are checked during configuration. The source and
diff are CMake configure dependencies, so changing either triggers the checks
on an incremental build. Nothing writes to IDF or `managed_components/`.

Each consumer regenerates its own copy from upstream on every configuration.
Git receives that output directory as an explicit `--work-tree`, so it applies
the diff relative to the generated tree even inside a repository's ignored build
directory. Without it, Git's subdirectory filtering can skip the entire patch
with a successful exit status; an external `/tmp` build does not expose this.
Both source hashes remain mandatory, including when a generated copy exists.

Recovery is a separate CMake project and never includes this hook. Its Wi-Fi
stack uses cryptographic primitives, but the image contains no X.509 parser
or TLS handshake implementation. Do not add the hook there.

When updating ESP-IDF, review a mismatch rather than just changing the hash.
Check upstream's parser, run the offline certificate regression, verify the
current MOEX chain and hostname with Stocks' CA, and build both images. Drop
the diff and hook when the bundled parser supports NumericString; keep the
regression fixture and update its unpatched expectation for the new baseline.

Native test and live verification recipes are in
[`firmware/test/mbedtls/README.md`](../../test/mbedtls/README.md).
