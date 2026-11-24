# Draft upstream report: accept NumericString X.509 name attribute values

Prepared 2026-11-24. Not submitted. Companion to [decision 0008](../decisions/0008-moex-tls.md).

## Suggested title

X.509 name parser rejects NumericString values in custom subject attributes

## Description

`x509_get_attr_type_value()` rejects ASN.1 NumericString (universal tag 0x12).
The certificate currently served by `iss.moex.com:443`, with SNI
`iss.moex.com`, uses NumericString for subject attributes INN (`1.2.643.100.4`)
and OGRN (`1.2.643.100.1`). Parsing this public leaf returns
`MBEDTLS_ERR_X509_INVALID_NAME + MBEDTLS_ERR_ASN1_UNEXPECTED_TAG` (-0x23E2),
before trust or hostname verification.

Reproduced with ESP-IDF v5.5.5's Mbed TLS 3.6.6, Espressif commit
`9d669eadb1955d348986b9280156710aaaadf79f`, built natively with its default
configuration. The accepted-tag lists inspected in upstream
[development at 0e2cc5f](https://github.com/Mbed-TLS/mbedtls/blob/0e2cc5fdac71ffbddb4b463189b042f8d7a5cf35/library/x509.c#L460)
and [3.6 LTS at 7405a0a](https://github.com/Mbed-TLS/mbedtls/blob/7405a0a8a068e6d84e758578e08eee0559348f46/library/x509.c#L442)
also omit NumericString. Both contain the check
`**p != MBEDTLS_ASN1_T61_STRING && **p != MBEDTLS_ASN1_PRINTABLE_STRING &&`.
This is a source inspection of upstream; the native reproducer ran against
Espressif's bundled version.

## Reproduction

Save the first public PEM certificate from:

```sh
openssl s_client -connect iss.moex.com:443 -servername iss.moex.com -showcerts </dev/null
```

Then initialize `mbedtls_x509_crt` and call
`mbedtls_x509_crt_parse_file(&certificate, "leaf.pem")`.
Expected: successful structural parsing of these custom name attributes.
Actual: -0x23E2. The saved regression fixture is
`firmware/test/mbedtls/fixtures/iss-moex-com.pem`, with source, date, serial
and SHA-256 documented beside it. This permits reproduction if the live
server changes. Public certificate only; no TLS session material is needed.

Captured 2026-11-24: leaf serial `103308847C1F462EB352BBDE18F3E59E`, DER
SHA-256 `A9594E7392C87FB0C882CB711DDAA0633D1756253E567EAAB079F83012A6391B`.
`openssl asn1parse` shows NumericString `7702077840` at DER offset 342 and
`1027739387411` at offset 365. No CA is needed to reproduce the parsing error.

## Proposed change

Name parsing already reads the OID and an attribute value, with an accepted
set of ASN.1 tags rather than a schema for every OID. Extend that set with
NumericString. Define `MBEDTLS_ASN1_NUMERIC_STRING` as 0x12 in the appropriate
ASN.1 header for upstream; our temporary downstream diff defines it privately
in `x509.c` to carry only one replaced source file. Add the condition:

```c
        **p != MBEDTLS_ASN1_NUMERIC_STRING &&
```

alongside the existing accepted-tag comparisons in `x509_get_attr_type_value()`.
After `mbedtls_asn1_get_len()` has checked the value bounds, validate its content
before assigning the value pointer:

```c
    if (val->tag == MBEDTLS_ASN1_NUMERIC_STRING) {
        for (size_t index = 0; index < val->len; ++index) {
            if ((*p)[index] != ' ' &&
                ((*p)[index] < '0' || (*p)[index] > '9')) {
                return MBEDTLS_ERROR_ADD(MBEDTLS_ERR_X509_INVALID_NAME,
                                         MBEDTLS_ERR_ASN1_INVALID_DATA);
            }
        }
    }
```

X.680 permits only ASCII digits 0-9 and space in NumericString. This check is
required for every attribute: CN fallback ignores the value tag, so accepting
NumericString without content validation allows a signed certificate without
SAN whose NumericString CN is `*.example.com` to pass wildcard hostname matching.
Rejecting invalid content during parsing prevents it from reaching that code.

Do not change name comparison, name rendering, writers, generic ASN.1 string
helpers, signature algorithms or certificate verification. Do not widen
DirectoryString schema definitions or add OID-specific policy in this fix.

## Validation

The committed native regression reproduces -0x23E2 with the original parser
and succeeds with the patch, retaining both NumericString tags and exact
INN/OGRN values. All 256 byte values are checked at both positions of a two-byte
NumericString; only digits and space parse. A signed test certificate without
SAN, issued by a test CA, whose NumericString CN contains `*.example.com` fails
parsing with -0x23E8. Without the alphabet check the same certificate parses
and verifies for `service.example.com` against that CA with return code 0 and
flags 0. Existing accepted tags still parse; unrelated tags and truncated
values fail. The separately fetched complete chain verifies with
the matching Russian Trusted Root CA and `iss.moex.com`, returning 0 with
verification flags 0. A wrong hostname and the Mozilla bundle without that
root both fail. The added root is an independent trust decision; parsing
support must not imply that any certificate is trusted.
