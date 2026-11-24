# 0008: MOEX certificate parsing and trust

- Date: 2026-11-24
- Status: accepted (owner, 2026-11-24)

## Context

The owner reported apparent Wi-Fi loss on firmware 0.1.96. The supplied
`wifi-icon/device-log.txt` contains successful HTTP 200 responses from other
services around Stocks' MOEX failures. Requests to `iss.moex.com` fail during
the TLS handshake with `-0x23E2`, delivered to the application as
`HOST_FAILED_NO_ANSWER` (-2). The header incorrectly used that request failure
as evidence that the network was unavailable.

## Findings

The separate Wi-Fi-icon investigation in `fix/wifi-icon-meaning` distinguishes
Wi-Fi association from individual request failures. That UI work is outside
this decision's parser patch; these failures are not evidence of lost Wi-Fi.

There are two independent TLS obstacles:

1. `-0x23E2` is `MBEDTLS_ERR_X509_INVALID_NAME` (-0x2380) plus
   `MBEDTLS_ERR_ASN1_UNEXPECTED_TAG` (-0x0062). The leaf certificate encodes
   subject attributes INN (`1.2.643.100.4`) and OGRN (`1.2.643.100.1`) as ASN.1
   NumericString (tag 0x12). ESP-IDF 5.5.5's mbedTLS name parser does not accept
   that tag. This is before certificate-chain verification, not an unsupported
   signature algorithm or a memory allocation refusal.
2. The server presents Russian Trusted Sub CA and Russian Trusted Root CA.
   Stocks 0.2.14 has no `certificates` entries or packaged roots. ESP-IDF's full
   Mozilla-derived bundle does not trust this root. After fixing parsing, the
   application also needs the root, scoped to its own requests.

## Evidence

On 2026-11-24, both the default negotiation and forced TLS 1.2 using
`openssl s_client -connect iss.moex.com:443 -servername iss.moex.com -showcerts`
reached 85.118.181.24 and presented the same leaf:

- Subject: `*.moex.com`, MOSCOW EXCHANGE.
- Serial: `103308847C1F462EB352BBDE18F3E59E`.
- Validity: 2026-04-06 08:06:04 UTC to 2027-04-06 08:06:04 UTC.
- SHA-256: `A9594E7392C87FB0C882CB711DDAA0633D1756253E567EAAB079F83012A6391B`.
- RSA 2048-bit leaf, RSA 4096-bit CA keys, SHA-256 with RSA signatures.
- `openssl asn1parse` identifies NumericString values at DER offsets 342 and
  365 in the leaf certificate.

The root's SHA-256 fingerprint is
`D26D2D0231B7C39F92CC738512BA54103519E4405D68B5BD703E9788CA8ECF31`.
It matches the root already packaged by Rates and Metals. OpenSSL verification
with this root, the served intermediate and hostname `iss.moex.com` succeeds;
verification against ESP-IDF's `cacrt_all.pem` fails with error 20, unable to get
the local issuer certificate.

The project's generated configuration enables TLS 1.2, RSA, ECDHE-RSA and
ECDSA exchanges and the full certificate bundle. ESP-IDF's configuration enables
RSA and SHA-256. Its default MPI limit is 1024 bytes, sufficient for these keys.
The parser's accepted tag list is unconditional: there is no sdkconfig switch
that enables NumericString. See the pinned
[mbedTLS parser](https://github.com/espressif/mbedtls/blob/9d669eadb1955d348986b9280156710aaaadf79f/library/x509.c)
and [ESP-IDF bundle documentation](https://docs.espressif.com/projects/esp-idf/en/v5.5.5/esp32c6/api-reference/protocols/esp_crt_bundle.html).

A native build of ESP-IDF 5.5.5's mbedTLS (3.6.6 plus Espressif patches,
commit `9d669eadb1955d348986b9280156710aaaadf79f`) reproduces exactly -0x23E2
when parsing the saved leaf alone. In an isolated experimental build, accepting
tag 0x12 in `x509_get_attr_type_value()` makes parsing succeed. Parsing and
`mbedtls_x509_crt_verify()` with the complete served chain (including its duplicate
leaf), the matching root and the hostname then return 0, with verification flags
0. This experiment used the
library's native default configuration, not a device handshake, and does not
modify the installed ESP-IDF or this firmware's TLS library.

The older owner-supplied `0.1.88-2026-11-23` run records MOEX HTTP 200 successes.
Its final success is Stocks poll 2 at 2026-11-23 18:31:41 UTC; the first -0x23E2
is at 18:46:44 UTC. Both have boot ID `1777735507`, running firmware 0.1.93.
Failures continue on 0.1.94. Thus the failure began before 0.1.95's memory guards
and larger interpreter arena. Certificate presentation changed, or a different
server path began serving this chain, on an unchanged firmware boot; this is an
inference because the earlier successful chain was not captured. The current
certificate's April validity date is not evidence that it was issued today.
Allocation error codes include X.509 -0x2880, SSL -0x7F00, ASN.1 -0x006A and
MPI -0x0010. The observed -0x23E2 instead identifies the rejected name tag and is
independently reproducible without heap pressure.

## Decision and consequences

The owner accepted the parser correction on 2026-11-24. Main firmware accepts
ASN.1 NumericString (0x12) wherever `x509_get_attr_type_value()` already accepts
PrintableString, with content restricted to X.680's alphabet: ASCII digits 0-9
and space only. The diff changes only `library/x509.c`: a private
`MBEDTLS_ASN1_NUMERIC_STRING` definition (the bundled header has none), one
additional accepted-tag comparison and a byte check after the bounded length
has been read. Invalid content returns `MBEDTLS_ERR_X509_INVALID_NAME` plus
`MBEDTLS_ERR_ASN1_INVALID_DATA`. It does not change name comparison,
rendering, writers, generic ASN.1 string helpers, algorithms, trust or hostname
verification. This corrects the demonstrated parser gap rather than hiding a
request failure or weakening verification.

Blind review found that accepting the tag alone lets a signed certificate
without SAN carry `*.example.com` in a NumericString CN and pass wildcard
hostname matching: CN fallback does not check the value tag. The alphabet
check rejects such a certificate during parsing, before hostname verification.
It applies to every NumericString attribute, including CN and custom OIDs.

Stocks 0.2.15's matching root is prepared separately in the catalog's
`fix/moex-ca`, commit `9b2932916e0c6009d0b9be02d0a66e21dcacf059`.
Both changes are necessary. The catalog root applies only to Stocks and still
requires the ordinary installation approval. This firmware change does not
add any globally trusted CA or modify the preinstalled catalog pin.

## Carrying the patch

Considered mechanisms:

| Mechanism | Assessment |
|---|---|
| Patch the installed IDF checkout | Small diff, but mutates a shared SDK, affects other projects and recovery, and requires extra setup on every machine and runner. Rejected. |
| Override the `mbedtls` component | Can reuse sources, but must reproduce or forward Espressif's component registration, Kconfig, ports and CMake integration. Too much integration surface for one changed source. Rejected. |
| Replace only the existing `mbedx509` target's source after IDF creates it | Keeps all upstream component settings; generates one verified patched source in the main build directory. Selected. |

`firmware/CMakeLists.txt` includes `firmware/patches/mbedtls/patch.cmake` after
`project()`. It verifies the original `library/x509.c` SHA-256, copies it into
`<build>/patched-mbedtls/library/x509.c`, applies the committed unified diff
with Git and verifies the resulting SHA-256. It then replaces exactly the
`mbedx509` target's `x509.c` entry. Unexpected hashes, patch failure, or a
changed target/source layout stop configuration with a diagnostic directing
the maintainer here. Both the upstream source and diff are configuration
dependencies, including for incremental builds. No installed or managed
vendor file is modified. The generated source preserves upstream's license.
Hashes and the exact base commit are in the patch's README.

CI on `031ee04` exposed a Git invocation error: the native test under `/tmp`
passed, while firmware configuration inside the checkout left the generated
file identical to upstream. From a repository subdirectory, `git apply` can
skip the absolute `--directory` patch paths and still return success. The
helper now sets the generated directory as Git's explicit `--work-tree` and
applies relative paths there. Each consumer still regenerates from the verified
upstream into its own build directory, and both hashes remain enforced. A native
regression creates ignored host, release and debug directories inside a Git
repository and checks repeated generation, including over a stale copy.

This hook runs for developer builds, IDE builds and both CI firmware flavors.
`.github/workflows/ci.yml` obtains IDF from the pinned
`espressif/idf:v5.5.5` container rather than a repository checkout step; no
runner preparation or image modification is needed. The native regression
runs there too, against that same bundled source. The ordinary pure-module
host suite retains its ability to run without ESP-IDF.

Recovery uses its own project and an explicit subset of shared components;
it never includes the patch hook. Its dependencies use mbedTLS crypto for
Wi-Fi but do not link the X.509 parser or a TLS handshake. This was checked in
the recovery link map, as well as by binary comparison below.

## Upstream status and removal

Inspected on 2026-11-24: upstream
[development at 0e2cc5f](https://github.com/Mbed-TLS/mbedtls/blob/0e2cc5fdac71ffbddb4b463189b042f8d7a5cf35/library/x509.c#L460)
and [3.6 LTS at 7405a0a](https://github.com/Mbed-TLS/mbedtls/blob/7405a0a8a068e6d84e758578e08eee0559348f46/library/x509.c#L442)
both omit NumericString from the accepted name-value tags. Their condition
includes `**p != MBEDTLS_ASN1_PRINTABLE_STRING &&`; no NumericString case
follows. This is source inspection, not a claim of running the reproducer
against those branches. A prepared [upstream report](../upstream/mbedtls-numericstring.md)
contains the reproducer and proposed correction; it has not been submitted.

On an ESP-IDF update, review any hash or target mismatch against the new
source; do not merely refresh the expected hash. Remove the local diff and
source-replacement hook when IDF bundles equivalent NumericString support,
then retain the certificate regression, adjust the original-parser expectation,
and repeat chain/hostname verification and both image builds. A compatible
MOEX certificate alone does not remove the general parser regression.

## Reproducible validation

The saved public leaf and native tests are under `firmware/test/mbedtls`.
Its [README](../../firmware/test/mbedtls/README.md) gives offline and live recipes.
The native project uses the same verified patched `x509.c`, upstream's default
native configuration, and the same crypto library for patched and original
parser tests. The offline tests need no network and do not depend on the
leaf's expiration date.

On 2026-11-24:

- Original parser: the real leaf fails with exactly -0x23E2. Relinking the
  positive test against the original `x509.c` object also fails with exit 1.
- Patched parser: returns 0 and preserves both NumericString OIDs and values.
  All previously accepted tags still parse; an unrelated tag and a truncated
  attribute fail. A changed upstream source is refused with a useful diagnostic.
- Review regression: all 256 byte values are tested in both positions of a
  two-byte NumericString; only digits and space are accepted. The saved signed
  certificate without SAN and with NumericString CN `*.example.com` is rejected
  at parse time with -0x23E8. Before the alphabet check, that same fixture
  parsed and verified for `service.example.com` against its test CA with return
  code 0 and flags 0. MOEX still parses with both exact INN/OGRN values.
- Live capture: all four served certificates, including the duplicate leaf,
  parse without skipped errors. With Stocks' prepared root and `iss.moex.com`,
  `mbedtls_x509_crt_verify()` returns 0 with flags `0x00000000`.
- Negative verification: `wrong.example` fails with flags `0x00000004`;
  substituting IDF's Mozilla bundle for Stocks' root fails with flags
  `0x00000008`. No verification callback overrides failures.
- Release builds with ESP-IDF 5.5.5 and `sdkconfig.defaults;sdkconfig.release`:
  main after alphabet validation 1,600,336 bytes (496,816 bytes free in its
  2 MiB slot), 64 bytes larger than the tag-only patch; recovery 855,952 bytes
  (61,552 bytes free in its 896 KiB slot). Recovery is byte-identical to both
  the pre-hook and tag-only builds in the same build directory (`cmp` succeeds),
  SHA-256 `1f32a5605b7e42776355625f6858f655e892c9ec7945d8a2efdd89a35533c7ad`.
- Existing checks passed: the full host suite, simulator (8 scenarios and 34
  pictures), `check-generated`, `check-api`, `contract check`, and core
  Gradle `check`; also CLI `pageTest`, `flashManifestTest`, `simTestMocksTest`
  and all three browser update/request/settings suites. On macOS the first
  core run hit a three-second handler timeout while the sanitizer's `atos`
  symbolizer processed an intentionally faulting application. The full core
  suite passed on retry with `ASAN_OPTIONS=symbolize=0` and
  `UBSAN_OPTIONS=symbolize=0`; sanitizer fault detection remained enabled,
  with no change to the checked source or test expectations.

Follow-up validation of the CI build-directory fix on the same date:

- Linux/amd64, `espressif/idf:v5.5.5`, Java 21 and wasi-sdk 34: unmodified
  `031ee04` passes all three native tests, then fails release configuration
  with the unchanged upstream hash. Verbose Git output confirms a skipped patch
  with exit status 0. The new build-directory regression fails against that helper.
- With the fix, all four native tests pass before each firmware flavor. The
  workflow's release and debug build commands and size reports complete. Main
  images are 1,600,688 and 1,687,840 bytes respectively; both recovery images are
  855,952 bytes. Each image passes its own partition check.
- The host, release and debug copies all have the expected patched hash and
  distinct build paths. Recovery compiles upstream instead, links no X.509 parser
  or TLS handshake, and its debug image is byte-identical to the baseline from
  the original recovery project. The shared IDF source retains its original hash.
- The external macOS release build, all four native tests and the full host
  suite also pass.

Native chain verification covers parsing, signatures, validity, trust and
hostname; it is not an ESP TLS handshake. No device was contacted, and no
upstream report was sent. Before claiming hardware acceptance, perform a
verified device handshake outside the owner's endurance run.
