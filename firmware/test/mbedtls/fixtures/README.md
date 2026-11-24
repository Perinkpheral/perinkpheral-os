# NumericString certificate fixtures

`iss-moex-com.pem` is only the public leaf certificate fetched on 2026-11-24
from `iss.moex.com:443` (85.118.181.24), with SNI `iss.moex.com`:

```sh
openssl s_client -connect iss.moex.com:443 -servername iss.moex.com -showcerts </dev/null
```

Take the first PEM certificate. Do not commit the session transcript or TLS
session material. The certificate is public test data, not a trust anchor.

- Subject: `*.moex.com`, MOSCOW EXCHANGE.
- Serial: `103308847C1F462EB352BBDE18F3E59E`.
- Validity: 2026-04-06 08:06:04 UTC to 2027-04-06 08:06:04 UTC.
- DER SHA-256: `A9594E7392C87FB0C882CB711DDAA0633D1756253E567EAAB079F83012A6391B`.
- INN (`1.2.643.100.4`): NumericString `7702077840`, tag at DER offset 342.
- OGRN (`1.2.643.100.1`): NumericString `1027739387411`, tag at DER offset 365.

The offline regression parses this certificate without verifying its validity
dates, so expiration will not invalidate the parser test. The separate live
chain check verifies dates, signatures, trust and hostname at the current time.

## NumericString wildcard regression

`numericstring-wildcard.pem` is a synthetic RSA-2048 certificate signed with
SHA-256 by `test-root.pem`, a synthetic RSA-2048 CA. It has no SAN extension
and its only subject attribute is CN `*.example.com`, encoded as NumericString
(tag 0x12). This is deliberately outside NumericString's digits-and-space
alphabet. Neither certificate is a production trust anchor.

Generated on 2026-11-24 with OpenSSL: create a test CA and a leaf CSR with
`/CN=*.example.com`, sign the leaf without extensions, change the subject CN's
UTF8String tag in its DER TBSCertificate to NumericString, then sign the
modified TBSCertificate with `openssl dgst -sha256 -sign` and rebuild the outer
certificate with that signature. The signature was independently checked with
`openssl dgst -sha256 -verify`. Private keys stay outside the repository.
Both certificates have 100-year validity; the passing regression checks parse
rejection and does not depend on their validity dates.

With the tag-only patch, native mbedTLS parsed this leaf and verified it against
the test CA for `service.example.com` with return code 0, flags 0 and no SAN.
The corrected parser rejects it with -0x23E8 before hostname matching.
