# SPDX-License-Identifier: MIT
"""Capture real simulator frames and the native setup view with fictional fixtures."""
import json
import os
from pathlib import Path
import random
import re
import shutil
import socket
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.request
import zlib

root = Path(__file__).resolve().parents[2]
output = Path(sys.argv[1]).resolve()
work = output / 'screen-work'
work.mkdir(parents=True, exist_ok=True)
tool = root / 'tools/perinkpheral'

def run(*args, **kwargs):
    subprocess.run([str(arg) for arg in args], cwd=root, check=True, **kwargs)

def png(path, width, height, black):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = bytearray()
    for row in range(height):
        rows.append(0)
        for column in range(width):
            rows.extend((20, 20, 19) if black(column, row) else (233, 231, 225))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))

def pbm(source, target):
    data = source.read_bytes()
    magic, header, pixels = data.split(b'\n', 2)
    width, height = map(int, header.split())
    if magic != b'P4':
        raise ValueError('Not a PBM frame')
    stride = (width + 7) // 8
    png(target, width, height, lambda x, y: pixels[y * stride + x // 8] & (128 >> (x % 8)))

def bmp(data, target):
    if data[:2] != b'BM' or struct.unpack_from('<H', data, 28)[0] != 1:
        raise ValueError('Not a one-bit BMP frame')
    offset = struct.unpack_from('<I', data, 10)[0]
    width, signed_height = struct.unpack_from('<ii', data, 18)
    height = abs(signed_height)
    stride = ((width + 31) // 32) * 4
    palette = 14 + struct.unpack_from('<I', data, 14)[0]
    zero_is_black = sum(data[palette:palette + 3]) < sum(data[palette + 4:palette + 7])
    def black(x, y):
        row = height - y - 1 if signed_height > 0 else y
        one = bool(data[offset + row * stride + x // 8] & (128 >> (x % 8)))
        return not one if zero_is_black else one
    png(target, width, height, black)

run(tool, 'glyph-packs', '--out', work / 'glyphs')
run(tool, 'preview', work / 'preview')
# Compile the real setup application with only its few host calls supplied natively.
english = json.loads((root / 'firmware/system/setup/strings/en.json').read_text())
russian = json.loads((root / 'firmware/system/setup/strings/ru.json').read_text())
strings = '#define STR_JOIN 0\n#define STR_OPEN 1\nstatic const char *app_string(int id) {\nstatic const char *const words[2][2] = {'
strings += ','.join('{' + ','.join(json.dumps(data[key], ensure_ascii=True) for key in ('join', 'open')) + '}' for data in (english, russian))
strings += '};\nreturn words[is_russian][id];\n}\n'
(work / 'strings.h').write_text(strings)
components = root / 'firmware/components'
run(os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
    '-I' + str(work), '-I' + str(root / 'sdk'), '-I' + str(root / 'sdk/lib'),
    '-I' + str(components / 'gfx/include'), '-I' + str(components / 'gfx/fonts'),
    '-I' + str(components / 'qr/qrcodegen'), root / 'docs/manual/capture_setup.c',
    components / 'gfx/gfx.c', components / 'gfx/font_square.c', components / 'qr/qrcodegen/qrcodegen.c',
    '-o', work / 'capture-setup')
# Use the actual recovery renderer with the browser fixture's target release.
run(os.environ.get('CC', 'cc'), '-std=c11', '-Wall', '-Wextra', '-Werror',
    '-I' + str(root / 'recovery/main'), '-I' + str(components / 'gfx/include'),
    '-I' + str(components / 'gfx/fonts'), '-I' + str(components / 'words/include'),
    '-I' + str(components / 'ui/include'), root / 'docs/manual/capture_recovery.c',
    root / 'recovery/main/screen.c', components / 'gfx/gfx.c',
    components / 'gfx/font_square.c', components / 'gfx/gfx_icons.c',
    components / 'gfx/gfx_icon_art.c', components / 'words/words.c',
    components / 'ui/notice.c', components / 'ui/kit.c', components / 'ui/list.c',
    components / 'ui/scrollbar.c', '-o', work / 'capture-recovery')
release = json.loads((root / 'docs/manual/fixtures/release.json').read_text())['tag_name'].removeprefix('v')
for language in ('en', 'ru'):
    for name, state in [('confirm-update', 'confirm'), ('recovery', 'waiting')]:
        target = work / (name + '-' + language + '.pbm')
        run(work / 'capture-recovery', language, work / 'glyphs/cyrillic.glyphs', state, release, target)
        pbm(target, output / (name + '-' + language + '.png'))
target = work / 'setup-en.pbm'
run(work / 'capture-setup', 'en', work / 'glyphs/cyrillic.glyphs', target)
pbm(target, output / 'setup-en.png')

randomizer = random.Random(417)
weather = {'current': {'temperature_2m': randomizer.randint(90, 170) / 10, 'weather_code': 2, 'wind_speed_10m': randomizer.randint(8, 20)},
           'daily': {'weather_code': [2, 3, 61], 'temperature_2m_max': [16, 15, 14], 'temperature_2m_min': [8, 9, 7], 'precipitation_probability_max': [20]}}
(work / 'mocks.json').write_text(json.dumps({'https://api.open-meteo.com/*': {'body': json.dumps(weather)}, '*': {'status': 503, 'body': ''}}))

def capture_app(app, language, shots, values=None, expected_status=None):
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
    address = 'http://127.0.0.1:' + str(port)
    state = work / (app.name + '-' + language + '-' + shots[0][0])
    if state.exists():
        shutil.rmtree(state)
    command = [str(tool), 'sim', str(app), '--port', str(port), '--language', language,
               '--glyphs', str(work / 'glyphs'), '--mocks', str(work / 'mocks.json'), '--state', str(state), '--at', '1795267500']
    with (work / (app.name + '-' + language + '-' + shots[0][0] + '.log')).open('w') as log:
        process = subprocess.Popen(command, cwd=root, stdout=log, stderr=subprocess.STDOUT, env={**os.environ, 'TZ': 'Europe/Paris'})
        try:
            deadline = time.monotonic() + 180
            while True:
                if process.poll() is not None:
                    raise RuntimeError('Simulator exited; see ' + str(log.name))
                try:
                    urllib.request.urlopen(address + '/api/dev/screen', timeout=1).read()
                    break
                except (urllib.error.URLError, TimeoutError):
                    if time.monotonic() > deadline:
                        raise RuntimeError('Simulator startup timed out; see ' + str(log.name))
                    time.sleep(0.2)
            for key, value in (values or {}).items():
                request = urllib.request.Request(address + '/api/dev/kv?key=' + key, data=value.encode(), method='POST')
                urllib.request.urlopen(request, timeout=10).read()
            for name, buttons in shots:
                if buttons:
                    run(tool, 'press', *buttons.split(), '--host', '127.0.0.1:' + str(port))
                # Check the application's saved response status before photographing its notice.
                if expected_status is not None:
                    status_file = state / 'store' / app.name / 'status'.encode().hex()
                    deadline = time.monotonic() + 15
                    while not (status_file.is_file() and status_file.read_bytes() == struct.pack('<i', expected_status)):
                        if time.monotonic() > deadline:
                            raise RuntimeError('Expected response status ' + str(expected_status) + '; see ' + str(log.name))
                        time.sleep(0.1)
                # Let the queued draw reach the panel after the response callback.
                time.sleep(1)
                data = urllib.request.urlopen(address + '/api/dev/screen', timeout=10).read()
                (output / (name + '-' + language + '.bmp')).write_bytes(data)
                bmp(data, output / (name + '-' + language + '.png'))
        finally:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()

not_ready = int(re.search(r'#define HOST_FAILED_NOT_READY\s+\((-?\d+)\)', (root / 'sdk/host/net.h').read_text()).group(1))
for language in ('en', 'ru'):
    capture_app(root / 'firmware/system/settings', language, [('settings', ''), ('appearance', '3 3 2')])
    apps = os.environ.get('MANUAL_APPS')
    if not apps:
        raise RuntimeError('Set MANUAL_APPS to a perinkpheral-apps checkout for the weather pictures.')
    capture_app(Path(apps) / 'apps/weather', language, [('not-set-up', '2')], {'every': '30 min'}, expected_status=not_ready)
    capture_app(Path(apps) / 'apps/weather', language, [('weather', '2')],
                {'city': 'Lyon', 'lat': '45.76', 'lon': '4.84', 'units': 'celsius', 'every': '30 min'}, expected_status=200)
