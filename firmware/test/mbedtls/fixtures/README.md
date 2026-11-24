# MOEX NumericString certificate

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
