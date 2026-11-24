# 0008: MOEX certificate parsing and trust

- Date: 2026-11-24
- Status: open

## Context

The owner reported apparent Wi-Fi loss on firmware 0.1.96. The supplied
`wifi-icon/device-log.txt` contains successful HTTP 200 responses from other
services around Stocks' MOEX failures. Requests to `iss.moex.com` fail during
the TLS handshake with `-0x23E2`, delivered to the application as
`HOST_FAILED_NO_ANSWER` (-2). The header incorrectly used that request failure
as evidence that the network was unavailable.

## Findings

The icon now follows Wi-Fi association with an obtained address. While the radio
is stopped for power saving, it retains the last connection attempt's outcome.
Server failures do not change it, and no extra network probes are added. Wi-Fi already
publishes these states; the poller's last-request flag and callback are removed. The simulator likewise distinguishes an
explicit no-network result from an individual server failure.

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

## Proposed fix and consequences

Carry a reviewed mbedTLS NumericString name-parser correction in the firmware's
dependency integration, or adopt an ESP-IDF version with that correction after
confirming its source and testing the saved certificate. No change to signature
algorithms or the heap budget resolves this parser error. Preserve chain and
hostname verification. An alternative is for MOEX to serve a compatible chain.

Stocks 0.2.15 packages the matching Russian root, reusing the existing catalog
certificate. That catalog change is necessary for this chain but is not a
standalone fix: deployment must also include the parser correction. It adds a
certificate authority requiring the usual installation approval and does not
extend trust for other applications. No live-device handshake or installation
was attempted, to preserve the owner's endurance run.

## Reopen when

Review when a compatible MOEX chain or an upstream parser correction is
available. Before claiming device acceptance, test both certificate parsing and
a verified live handshake on a device outside the endurance run.
