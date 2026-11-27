# Simulator

Runs one application on a computer using the firmware's interpreter, Perinkpheral OS API,
UI kit and window manager. It exposes the rendered framebuffer and simulated input.

Requires Java 21 or later, wasi-sdk selected by `WASI_SDK_PATH`, a C compiler and Make.
Use `--glyphs <directory>` to load glyph packs for text beyond the built-in font coverage.

    tools/perinkpheral sim <application folder> [--language ru] [--mocks mocks.json]

The simulator listens at `http://127.0.0.1:8401`. Run commands separately:

```sh
tools/perinkpheral --host 127.0.0.1:8401 shot screen.png
tools/perinkpheral --host 127.0.0.1:8401 press 2
tools/perinkpheral --host 127.0.0.1:8401 log
tools/perinkpheral --host 127.0.0.1:8401 serve
```

The CLion plugin can use the same address.

The binary's `--port 0` takes a port of the system's choice and `--port-file <file>` writes
its number there once the simulator listens. `sim-test` starts every simulator this way, so
no two of them, and no other program, can be given the same port.

| Device | Simulator |
|---|---|
| Panel | a frame in memory, `/api/screen`; its address in a browser shows the page of `perinkpheral serve` - the screen, the buttons, the log, and the requests below as buttons |
| Buttons | `/api/dev/button` |
| Store (NVS) | a folder, a file per key, the device's limits |
| Files (LittleFS) | the built package copied to `<state>/apps/<name>`; files written by the application remain there between runs unless replaced by package files; `--state <directory>` selects the state folder |
| Network | that of the program that started it - tools/perinkpheral, or the CLion plugin in the IDE's own process - asked a JSON line an exchange on `--fetcher-port`; a redirect is followed by the simulator itself, by the firmware's rules (`app_manifest_is_same_host`, `app_manifest_allows_url`, `app_manifest_redirect_keeps_headers`): a GET three times at most, a request of the application's own inside its hosts, its application headers dropped on a change of host or a downgrade from HTTPS to HTTP; an application's own certificate authorities (`certificates`) trusted for its requests beside the computer's; answers replaced by mocks, whose redirects are delivered as final responses and are not followed |
| Clock | the computer's, in real time; moved on by `/api/sim/forward` |

Beyond the device's API:

| Request | Effect |
|---|---|
| `POST /api/sim/background` | the application leaves the screen; from then on it is loaded only for what comes to it, as on the device |
| `POST /api/sim/foreground` | the application takes the screen again |
| `POST /api/sim/intent?kind=started\|time_changed\|network_up\|installed\|updated` | started, time_changed and network_up require the corresponding manifest subscription; installed and updated deliver lifecycle callbacks without an intent subscription |
| `POST /api/sim/alarm?id=N` | alarm N comes now |
| `POST /api/sim/forward?seconds=N` | the clock and every deadline move on |

Mocks: a JSON object of address to answer. An address ending in `*` matches
every address that begins with the rest. With `--mocks`, all requests stay offline:
an unmatched request is logged and delivered as `HOST_FAILED_NO_ANSWER`. An unreadable
or invalid mocks file stops the simulator with a non-zero exit status. Omit `--mocks`
to use the computer's network.

The header starts connected. An explicit `HOST_FAILED_NO_NETWORK` mock crosses
out its Wi-Fi icon; an HTTP response restores it. Other request failures leave
the icon unchanged, as a server failure does not mean Wi-Fi was lost.

    {
      "https://api.open-meteo.com/*": { "status": 200, "file": "forecast.json" },
      "https://example.com/api/state": { "status": 401, "body": "{}" }
    }

Wi-Fi loss, address acquisition and deliberate radio cycling are not modeled.
The `network_up` endpoint retries requested manifest polls that failed in transport,
then delivers subscribed news. Healthy and never requested manual polls stay idle.
The simulator uses the firmware poll scheduler for pending coalescing, the accepted-call
window and list rounds. Manual polls have no automatic deadline or settings trigger.
Normalized Retry-After and the bare-429 fallback defer every manifest-poll trigger on
uptime; raw-request responses do not create poll cooldowns. The firmware host tests
cover power-profile reconnect classification and transport failures.

Not simulated: the menu of several applications, the system API except
`sys_setting_get`, `sys_setting_set`, `sys_setting_range`, `sys_time_set`,
`sys_draw_logo`, `sys_logo_width` and `sys_logo_height`, e-paper
refresh timing, sleep, the battery or firmware/recovery updates. The device's
112 KB application area (the module's memory and the interpreter's together) is not
enforced as a combined arena; the module's 64 KB linear-memory limit still applies.
Use the build's memory report and a real device to check interpreter headroom.

Build: `make` for this computer (a C compiler and Java 21 or later for import generation). `make platforms`
builds what the CLion plugin carries: `build/macos` (arm64 and x86_64, on macOS),
`build/linux-x86_64` and `build/linux-aarch64` (static, musl, cross-built with
[zig](https://ziglang.org)). Windows is not supported yet.

Mocks may add `"method": "POST"` and `"request_body": "the expanded text"` to
match a request as well as its URL. `"body"` remains the response body. Manifest
poll bodies are expanded with current settings at dispatch, including the selected
`each` item; picked and streamed responses use the same method and body.
