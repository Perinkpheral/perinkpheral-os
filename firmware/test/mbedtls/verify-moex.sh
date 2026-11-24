#!/bin/sh
# SPDX-License-Identifier: MIT
set -eu
if [ "$#" -ne 3 ]; then
    echo "Usage: verify-moex.sh host-build-directory stocks-root.pem output-directory" >&2
    exit 1
fi
host_build=$1
trusted_root=$2
output_dir=$3
mkdir -p "$output_dir"
openssl s_client -connect iss.moex.com:443 -servername iss.moex.com -showcerts \
    </dev/null >"$output_dir/served.txt" 2>"$output_dir/openssl.log"
# Keep every served certificate, including duplicate leaves, in the original order.
awk '/-----BEGIN CERTIFICATE-----/ { certificate = 1 } certificate { print } /-----END CERTIFICATE-----/ { certificate = 0 }' \
    "$output_dir/served.txt" >"$output_dir/served-chain.pem"
"$host_build/verify_chain" "$output_dir/served-chain.pem" "$trusted_root" iss.moex.com
if "$host_build/verify_chain" "$output_dir/served-chain.pem" "$trusted_root" wrong.example; then
    echo "Incorrect hostname unexpectedly verified" >&2
    exit 1
fi
echo "Served chain and hostname verified; incorrect hostname refused."
