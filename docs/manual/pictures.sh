#!/bin/sh
# SPDX-License-Identifier: MIT
# Run outside the sandbox. Write every intermediate and review image under the chosen folder.
set -eu
manual_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(CDPATH= cd -- "$manual_dir/../.." && pwd)
output=${1:?Usage: pictures.sh OUTPUT_DIRECTORY}
mkdir -p "$output"
output=$(CDPATH= cd -- "$output" && pwd)
cd "$root"
: "${CAD_PYTHON:?Set CAD_PYTHON to the Python with tools/case/requirements.txt installed}"
# Continue independent stages after a failure so the review still gets useful pictures.
failed=0
python3 "$manual_dir/screen_pictures.py" "$output" || failed=1
node "$manual_dir/web_pictures.cjs" "$output" || failed=1
if "$CAD_PYTHON" "$manual_dir/export_model.py" "$output/model.json" && \
    "${BLENDER:-blender}" -b --python "$manual_dir/render_device.py" -- "$output/model.json" "$output"; then
    node "$manual_dir/hardware_pictures.cjs" "$output" || failed=1
else
    failed=1
fi
[ "$failed" -eq 0 ] || exit "$failed"
# Preserve the accepted raster dimensions before replacing any source picture.
python3 - "$output" "$manual_dir/source/pictures" <<'PY'
import json
from pathlib import Path
import struct
import sys

output, sources = map(Path, sys.argv[1:])
dimensions = {}
for source in sorted(sources.glob('*.png')):
    generated = output / source.name
    if not generated.is_file():
        raise SystemExit(f'Missing picture: {generated}')
    old_size = struct.unpack('>II', source.read_bytes()[16:24])
    new_size = struct.unpack('>II', generated.read_bytes()[16:24])
    dimensions[source.name] = {'accepted': old_size, 'generated': new_size}
    if old_size != new_size:
        raise SystemExit(f'Picture dimensions changed: {source.name}: {old_size} -> {new_size}')
(output / 'picture-dimensions.json').write_text(json.dumps(dimensions, indent=2) + '\n')
PY
# Reviewed assets beside the source let CI print without CAD, Blender or simulator dependencies.
for name in device-base device-front device-port; do
    cp "$output/$name.png" "$manual_dir/source/pictures/"
done
for name in device-buttons device-back device-switch device-rescue device-recovery device-usb; do
    cp "$output/$name.svg" "$manual_dir/source/pictures/"
done
for language in en ru; do
    for name in setup settings appearance not-set-up weather recovery confirm-update \
        web-catalog web-apps web-place web-place-search web-list web-wifi web-settings web-firmware web-recovery; do
        cp "$output/$name-$language.png" "$manual_dir/source/pictures/"
    done
done
exit "$failed"
