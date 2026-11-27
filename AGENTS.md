# AGENTS.md

Working notes for coding agents: recipes and the constraints that are
not visible in the code. The specification is [CLAUDE.md](CLAUDE.md); read it
first, and where the two disagree it wins. Everything learned about the
panel is in [hardware/README.md](hardware/README.md).

## Recipes

The toolchain is ESP-IDF v5.5 (verified on v5.5.5) with its `export.sh`
sourced, wasi-sdk (verified with 34) with `WASI_SDK_PATH` pointing at it,
and Java 21 or later for the tools. The firmware build needs all three:
the firmware carries its system applications in wasm, and the tools make
its table of imports and its page.

The board is on its native USB-Serial/JTAG (VID 0x303A PID 0x1001):
`/dev/ttyACM*` on Linux, `/dev/cu.usbmodem*` on macOS. The name changes
with the USB socket, so look it up; the recipes call it `$PORT`. The port can
disappear during unplugging, reset or light sleep. Discover it again after
the board enumerates; firmware restores USB when it wakes.

Build (the IDF project is `firmware/`):

```bash
. "$IDF_PATH/export.sh"
cd firmware
idf.py build
```

This also builds the separate recovery project into `build/recovery/` and checks
main and recovery against their own partitions.

The image of the `apps` partition is the applications `firmware/preinstalled.properties`
names, taken from the catalog repository at the commit it pins. The build downloads them
from raw.githubusercontent.com the first time and keeps them in the build folder; with no
network, point `PERINKPHERAL_APPS` at a checkout of perinkpheral-apps that has the commit
(any branch: the files are read from the commit, not the working tree). With neither, the
build stops and says so. To move the pin to the catalog's main:
`tools/perinkpheral preinstalled bump firmware/preinstalled.properties`, which prints each
application's version before and after; commit the file it changed.

Preinstallation is enabled by default. Set `CONFIG_APP_BUNDLE_APPS=n` to generate an
empty `apps` image. A full flash still replaces the `apps` partition. Embedded system
applications are unaffected.

`IDF_SKIP_CHECK_SUBMODULES=1` in front of `idf.py` skips a git check
that needs the network. After editing `main/Kconfig.projbuild` or
adding a component: `idf.py reconfigure`.

Flash: `idf.py -p $PORT flash` (bootloader, partition table, otadata,
recovery, main, and the applications' partition `apps` with the preinstalled
applications - what is installed there is replaced). Both `flash` and `app-flash` preserve `nvs`;
`app-flash` also preserves recovery and the apps partition, and selects recovery
through blank OTA metadata. Press BACK in recovery to try the installed main.
`recovery-flash` writes only recovery over USB; it preserves main, OTA metadata and
all data partitions. Enter recovery from the device page after this maintenance flash.
Builds check each image against its own partition. Use the generated migration
manifest to preserve data when moving an existing device to the recovery layout;
never erase the whole flash or write apps.bin during migration.
`erase-flash` erases all flash;
flash again to boot with defaults. For the one-time migration, after building, use
`ESPPORT="$PORT" idf.py -B <build> migration-flash` from `firmware/`. This writes
bootloader, partition table, OTA metadata, recovery and main; no data images.
Check the installed `esptool` version: v5 spells commands with dashes
(`write-flash`, `read-flash`); v4, including some IDF environments, uses underscores.

Console capture. Opening the port does not reset the board, so a
capture can watch a running device; `idf.py flash` resets it, and the
port disappears for a moment while it does:

```bash
tools/perinkpheral console "$PORT" --seconds 30
```

Host tests: `cd firmware/test/host && make test`. With `WASI_SDK_PATH`
set they include the ABI 3 snapshot and frozen package checks, `test_app`,
which builds a real module (`firmware/test/host/apps/probe`, from the repository root)
and runs it in the real interpreter, and
`test_delivery`, which checks response delivery to an application. ESP-IDF is
not needed; Java 21 or later is, even without wasi-sdk, because `make test`
also runs the tools' language, layer and city-table checks.

Firmware and application update clients have separate suites, run from the repository root:

```bash
tools/cli/gradlew -p tools/core firmwareUpdateTest
node firmware/test/web/test_firmware_update.js
node firmware/test/web/test_app_requests.js
tools/cli/gradlew -p tools/cli pageTest
node firmware/test/web/test_app_settings.js
```

The first tests the shared Kotlin client used by the CLI and CLion plugin;
the second tests the browser update and manual recovery flows; the third tests
catalog and app-install retries, their limits and permanent refusals.
`pageTest` checks the development page. `test_app_settings.js` checks application
settings fields, including saved empty values and unavailable settings. All five
suites run separately from `make test`. The firmware's simulator cases are separate too:
`tools/perinkpheral sim-test firmware/test/sim`. Its scenarios run several at a time, one
a processor; `--jobs N` sets how many and `--jobs 1` runs them one by one. A `wait` step
is real time on the computer's clock. Use `forward` for deterministic clock advancement.
`"serial": true` prevents overlap with other scenarios but does not remove the timing
dependence of a `wait` step. If an expected PNG is missing, `sim-test` writes it and
fails even without `--update`. Review that picture before committing it. `--update`
also replaces existing expected pictures.

`tools/cli/gradlew -p tools/core check` also runs the builder safety and endurance
observer tests, plus catalog publishing against local Git repositories. They use
temporary files and simulated endpoints, not a device or live GitHub writes.

Applications against the contract, on this computer: `tools/perinkpheral app-check <folder>`
builds every application of the folder, then runs the capability checks (sessions, store,
timers, network, drawing, notices, clock) and each application's own tests (`<application>/test/*.c`) on
non-system applications, compiled with the sanitizers against the stand-in host of `sdk/check`. Applications with `"system": true` skip
both capability checks and their own tests. It needs a C compiler and wasi-sdk.
`--only <capability>` and `--app <name>` narrow it. The checks know no application: what one
needs to be checked - a time, a store, answers to its requests - is its `test/check.json`.
A call added to the contract is given its stand-in in `sdk/check/standin.c`, or an
application that imports it does not link there.

The tools are `tools/perinkpheral` (Kotlin: `tools/cli` over the library `tools/core`; Java 21, built on first use).
An application: `tools/perinkpheral build sdk/apps/<name> --out <dir>`
builds `app.wasm` without a C library, with memory sized to its stack and data,
refuses a module that imports anything `sdk/host_api.h` does not
declare, and checks `manifest.json` by Perinkpheral OS's rules. The firmware
build builds the system applications of `firmware/system` this way; `sdk/apps`
holds the SDK's sample, which no firmware image carries.

Publish applications to a user-owned public GitHub repository:
`tools/perinkpheral publish <application-folder> --repo owner/repository`.
Use the directory containing the applications. The repository must already exist
and `gh auth login` must have write access. This replaces only
`catalog/` on its default branch; include every application to keep published.
The printed repository URL can be added under Catalog > Sources on the device page.

Views before flashing, from the repository root: `tools/perinkpheral preview [dir]` compiles
`gfx`, the window manager, `firmware/main/screens.c` and `recovery/main/screen.c` natively
(`tools/preview/Makefile`) and writes a PNG per screen. Inspect the rendered frames, then verify them on the panel.

Generated files, from the repository root:

```bash
tools/perinkpheral check-generated
tools/perinkpheral check-api
tools/perinkpheral guide-units missing ru
tools/perinkpheral check-limits docs/guide/source/guide.html
tools/perinkpheral guide
tools/perinkpheral guide --language ru
```

The user manual shares the guide builder: `tools/perinkpheral manual` and
`tools/perinkpheral manual --language ru`. Its English source and translation pairs
are `docs/manual/source/manual.html` and `manual.ru.txt`; `manual-units missing ru`
checks translation coverage. Rebuild and visually review its pictures with the
scripts described in `docs/manual/README.md`. PDFs are local outputs, not committed.

`check-generated` compares the committed fonts, icon table, marks, spelling table,
city table, map land data and guide screens with fresh output. The map check requires
the cached source, network access, or `--source <geojson>` containing the pinned source.
Text files compare by bytes; PNGs compare by pixels. It also checks the page's embedded mark and wordmark. The
sources are `docs/guide/source/guide.html` and `docs/guide/source/guide.ru.txt`;
translated HTML is an ignored intermediate in the same folder. PDFs are generated
in `docs/guide/` locally and are not committed. Rebuild both after an edit. `guide-units missing ru` prints missing
units but exits successfully; CI requires empty output.

CI's Generated files and guides job runs on a self-hosted runner labelled
`perinkpheral`, with Java 21, a C compiler, Chromium, poppler-utils,
fonts-dejavu-core, fonts-liberation and fonts-noto-core.
It builds all four PDFs from the checked sources and complete translations, and
attaches all four PDFs and the glyph packs to each release. It keeps glyph sheets as
run artifacts. The device page is assembled in CI and in each firmware build;
there is no committed assembled copy. The flasher index is already generated by
`flasher.yml` after a release. Frozen ABI packages are inputs; never regenerate
them to make a failing compatibility check pass.

For glyph sheets in a shell without a graphics session, set
`JAVA_TOOL_OPTIONS=-Djava.awt.headless=true`; CI sets it for the generators.

Network development tools: `tools/perinkpheral`
with `PERINKPHERAL_HOST` set to the device's address (the second page of About
shows it).
Injected presses, opening/running applications and USB control require a
development build. Network screen capture, status, log and firmware OTA also work
in release builds.

- `shot out.png` saves the frame the panel shows;
- `press 3 3 2 --shot out.png` presses buttons (`1`-`4` short, `1L`-`4L`
  long) and waits for the panel after each;
- `open clock` puts an installed application on the screen;
- `run ../perinkpheral-apps/apps/clock --shot out.png` builds the application, uploads it,
  restarts the device and opens it - the loop for working on an
  application, no reflashing;
- `flash` uploads `firmware/build/perinkpheral.bin` and waits for the
  device to come back; it asks for OK on the device before installation.
  `flash --apps` also rebuilds and uploads the applications of `sdk/apps`
  through the development routes, so the uploaded firmware must be a
  development build. Compatible ABI 3 additions do not require reinstalling
  existing applications; rebuild and reinstall an application when it needs
  the new API. Catalog applications are installed from the page.
  `status` prints device status; `log` prints device logs.

Manual stress ramps on a bench device:

```bash
tools/perinkpheral stress-run --out <empty-directory> --host <device> \
    --bench <computer-LAN-address> --axes a,b,c,d,e,f,g --serial <port> --quick
```

`--host` defaults to `PERINKPHERAL_HOST`. `--bench` is inferred only with one
unambiguous LAN address. `--axes`, `--serial` and `--quick` are optional. A, B and G
work on release builds; C-F require development tools and wasi-sdk. C-F and B's
install part require a captured, restorable remembered application or the Boolean
flag `--allow-remembered-change`; current firmware does not expose the remembered name.
The runner owns its hostile HTTP server and builds `tools/stress/apps` with the application builder.
It records `steps.jsonl`, optional `serial.log`, a cleanup inventory and `report.md`;
an axis failure, interruption or cleanup error exits 1. Ctrl-C waits for cleanup.
It never resets to defaults, changes Wi-Fi or injects OK. It restores `time_auto`,
removes temporary applications with `forget=1` and removes its separate glyph pack.
Existing application names and retained stores are protected. Source 0 is read
through the device; unavailable catalog installs are skipped. No GitHub access from
the computer is required. See [load testing](docs/testing/load.md) for the ramps
and qualification limits. Run directories contain device settings; keep them private.

Verify a change on the real device this way, not only in the preview.
With no network, `tools/perinkpheral usb $PORT --keys 3,2 --out
shot.png` does presses and the frame over the USB console (`1`-`4` a
short press, `q` `w` `e` `r` a long press of button 1-4).

Fonts: `tools/perinkpheral font <ttf or otf> --px N --name <id> --out
firmware/components/gfx/fonts/<id>.h` rasterizes a font one bit a pixel
(printable ASCII) into a `gfx_font_t` for `gfx_text_font()`; the glyph
packs are `tools/perinkpheral glyph-packs --out <dir>`, the spelling
tables `tools/perinkpheral spelling --out firmware/components/gfx/spelling.h`.
Departure Mono is the default UI font. Its source is
`assets/fonts/DepartureMono-Regular.otf`, rasterized once at 11 px with
`--px 11 --name departure_11`; its sizes use whole bitmap multiples. The selectable
Jersey family uses four separately rasterized faces. Font sizes and family mappings
are defined in `firmware/components/gfx/gfx.c`. The generated ASCII headers are committed.

Firmware without the cable, once the device is on a network:
`curl -X POST http://<address>/api/ota/recovery`, wait for recovery, then
`curl --data-binary @firmware/build/perinkpheral.bin http://<address>/api/ota`, and OK on the device's own screen within a
minute (unanswered, the upload is refused). The device restarts into the new image and
keeps it once its web server is up and it is connected, or, with no saved network and Wi-Fi
enabled, its setup AP is up with 20 seconds of uptime. With deliberately disabled Wi-Fi, the web server and 20 seconds suffice without
network reachability; the next update requires recovery, entered with BACK at reset; `GET /api/status` says
whether it is still on probation. The address is on the second page of About.
For manual recovery without a planned upload, use **Device > Enter recovery** on
the page or `POST /api/ota/recovery?manual=true`. This skips preparation. BACK,
labelled **Exit recovery**, validates and tries the installed main with probation;
a damaged image must be replaced by an upload. Recovery always keeps its open
`perink-recovery` access point at `192.168.4.1` alongside the saved Wi-Fi connection.
Only `/`, `GET /api/status` and `POST /api/ota` are served there; the main firmware's
log, settings and development routes are unavailable until main runs again.
USB is not required for ordinary OTA when the battery is charged. Removing USB with
the battery connected is not a power-cut test.

The log without the cable: `curl http://<address>/api/log` gives the
last 4 KB of it; `l` in the development console prints the same.

`firmware/CMakeLists.txt` takes the version from `APP_VERSION` when it
is set.

From `firmware/`, with the toolchain environment configured, build a release without
development tools (`sdkconfig.release`) and with the version alone:

    APP_VERSION=$(cat ../VERSION) idf.py -B build-release \
        -D SDKCONFIG=build-release/sdkconfig \
        -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.release" build

`build-release/perinkpheral.bin` is the asset of the release on GitHub.
This is a local build recipe, not the CI version calculation: CI takes the
greater of `VERSION` and the latest version tag with its patch incremented.
Every push to `main` publishes a release after the checks pass; other branches
build without publishing (`.github/workflows/ci.yml`).

### An IDE (CLion, or any that reads CMake)

Three projects, each opened as a folder. Application projects and the repository-root
project need `WASI_SDK_PATH` in the environment supplied to CMake (CLion: Settings,
Build, CMake, Environment). The firmware project also accepts
`-DWASI_SDK_PATH=<sdk-directory>` and can discover an installed SDK.

- **An application, or a folder of them.** `tools/perinkpheral new <directory> --author "Your name"` creates an application
  containing a `CMakeLists.txt`; the perinkpheral-apps repository has one for all of its own. The code is
  understood with the SDK's headers, `strings.h` and `pictures.h`, and every application has
  build targets: `<name>` (the code compiled), `<name>-package` (the package, with the build's
  report), `<name>-run` (build, upload, restart the device and open it) and `<name>-serve` (the
  live page). The device's address is `PERINKPHERAL_HOST`, in the environment or as a CMake
  variable. The module behind it is `sdk/cmake/perinkpheral.cmake`; the targets call
  `tools/perinkpheral`.
- **This repository's root.** The sample and the system applications the same way, and the
  project's tools as targets: `host-tests`, `guide`, and with `IDF_PATH` set `firmware` and
  `firmware-send`. The firmware's own code is not part of this project.
- **The `firmware` folder**, for the firmware's code. Outside ESP-IDF's environment,
  `firmware/cmake/environment.cmake` discovers ESP-IDF and wasi-sdk and configures their
  tools. Set `IDF_PATH` and `WASI_SDK_PATH` in the IDE environment, or pass
  `-DIDF_PATH=<idf-directory>` and `-DWASI_SDK_PATH=<sdk-directory>` to CMake.
  The IDE builds into a directory of its own, not `firmware/build`,
  which is `idf.py`'s; what is sent to a device is `idf.py`'s build. Flashing and the monitor
  stay `idf.py`'s and `tools/perinkpheral`'s.

## Constraints

Code:

- SPDX header in every source file, C11, 4 spaces, `snake_case`.
  Commit bodies say why.
- Code is written to be read by a person. Names carry the meaning:
  `buffer_x`, `row_height`, not `a`, `b`, `t`. Numbers that mean
  something are named constants. Comments only for what cannot be read
  off the code: why, not what.
- Logic that does not need hardware is pure and host-tested: `gfx`,
  `btn_core`, `refresh_policy`, `appconf` and the rest that
  `tools/perinkpheral check-layers` lists as pure. `display.c` and
  `buttons.c` only move it onto GPIO and tasks.
- `settings_start()` falls back to defaults for invalid stored values and `reset_early_init()` runs before anything opens
  NVS. Main's `settings.c` owns Perinkpheral OS's settings; recovery's `settings.c`
  reads them through a read-only partition descriptor. A factory
  reset erases it whole (`reset.c`), and `app_kv.c` moves what an old
  firmware kept there;
  applications get their own namespaces through Perinkpheral OS API `kv_*`.
- `sdk/host_api.h` is the only contract between Perinkpheral OS and applications; it
  includes the declarations split by subject in `sdk/host/*.h`. Applications include
  the umbrella only; a declaration may move between those files without an ABI change.
  An import has three firmware integration points: the appropriate
  subject header, its `HOST_FUNCTION` in the `firmware/components/appsys/app_api_<area>.c` of its kind (the table
  that links it is made from the header), and a case in
  `firmware/test/host/apps/probe` with its check in `firmware/test/host/test_app.c`. Also implement its native stand-in in
  `sdk/check/standin.c` for capability checks and record the addition with
  `tools/perinkpheral contract snapshot`. An export
  Perinkpheral OS calls is added to the header and called in `firmware/components/appsys/app.c`, whose
  count of them stops the build until it is. ABI 3 is held as frozen:
  changing an existing signature, meaning, privilege, layout or constant
  requires a new ABI, not just a firmware version. Exceptions are growth of
  `HOST_ICON_COUNT` through contiguous appended icon IDs and extension of
  `host_answer_info_t` after its unchanged prefix, preserving alignment. Until the first
  public release, the owner may authorize other ABI 3 changes, recorded with
  `tools/perinkpheral contract snapshot --unfrozen`.
  Record every added call with `contract snapshot`: the check refuses an unrecorded one. Commands use subject
  then verb (`kv_get`, `gfx_draw_text`); queries name the subject and what
  is asked (`gfx_width`, `time_now`); events are `app_on_<what happened>`
  (`app_on_load`, `app_on_picked`). ABI 3 requires firmware 0.1.30;
  `HOST_SINCE_` stamps only additions after that floor. The contract snapshot
  is `sdk/abi/3.txt`; fixture hashes are separate, in `sdk/abi/3/fixtures.txt`.
- The application is not trusted: `app.c` and the `app_api_*.c` check
  every offset and length against the module's memory before using it.
  Keep them free of ESP-IDF; what they need from the device comes in as
  `app_services_t`.
- `firmware/components/wasm3/wasm3` is upstream v0.9.0 with two local changes,
  described in `firmware/components/wasm3/README.md`: loops yield, so that an
  application stuck in one can be stopped, and its memory comes from
  the application area. Configure the interpreter
  from the component's `CMakeLists.txt`; do not edit it further.
- The generated `managed_components/` directories inside the firmware and recovery
  projects are downloaded; do not edit them.
- `gfx_text_sq_ex()` draws the square font: `scale` is the stroke in
  pixels and the gap is a quarter wider, rounded, so scale 5 gives the 27 px
  square of the logo.

Display:

- `firmware/components/epd/epd.c` follows Waveshare's `epd4in2_V2` command sequence
  with the adaptations recorded in `hardware/README.md`. Preserve these three measured constraints
  (details in `hardware/README.md`):
  - CS is toggled per byte by GPIO. CS held low over a block of data
    starts a refresh that never ends: BUSY stays high for 30 s and more
    and the image fades to grey.
  - The bytes are clocked out by hand (`tx()` in `epd.c`), not by the
    SPI driver: mode 0, at least 75 ns to each half of the clock, CS
    high between bytes. Preserve the twelve-NOP delays, which provide at least 75 ns per
    clock half-cycle at 160 MHz.
  - Every BUSY wait is bounded (init 1 s, full 4 s, partial 3 s). A
    refresh that times out cuts the controller with a hardware reset and
    sleeps it; the waits of an init go on to the next command. After deep
    sleep (`0x10`) BUSY stays high for good; the next show always
    starts with a hardware reset, which is also how the panel wakes.
- A byte takes about 8 microseconds, a frame 0.12 s.
- Only `display.c` touches the panel. Each drawing component owns its framebuffer
  and submits complete frames to `display_show(frame, reason)`; the
  service keeps a copy of what the panel shows, leaves the panel alone
  when the frame is the same, and may answer "deferred" - then offer
  the frame again later.
- The first image after boot is full: it fills the RAM 0x26 that
  partial updates compare against. `epd.c` sleeps the panel at the end
  of a full show, mode 1 so that RAM is kept; after a partial one a
  deadline of the main event loop does it 1.5 s later (`display_rest()`), and
  another schedules the keep-alive refresh at `refresh_keepalive_h`, configurable
  from 1 to 168 hours and defaulting to 24 hours (`display_keepalive()`).

Board and system:

- GPIO16/GPIO17 are UART0 TX/RX and here they are BUSY and CS, so the
  console is on USB-JTAG (`sdkconfig.defaults`). The efuse that
  silences the ROM boot log is irreversible: do not touch it.
- IOMUX SPI2 on the C6 is CS0 = GPIO16 and MISO = GPIO2, which are our
  BUSY and button 3. The panel uses software SPI through GPIO; SPI2's IOMUX assignments do not apply.
- `CONFIG_FREERTOS_HZ=1000`. At the default 100 Hz `pdMS_TO_TICKS(5)`
  is zero and the button task starves everything else; the boot log
  then stops right after the button pins are reported.
- `board_init()` drives the antenna switch before the radio starts.
- Neither the partition table nor the bootloader can be changed over
  the air. Only `recovery/main/ota.c` writes the main slot; `firmware/main/ota.c` selects recovery and marks probation (`esp_ota_*`);
  others only ask which one runs. A new image is confirmed
  by the probation task, never at boot: confirming early would keep an
  image that cannot be reached to replace it.

## A device that does not answer

In the main firmware, hold buttons 1 and 4 (UP and BACK) together for ten seconds: the device restarts, whatever
its tasks are doing. It is counted in interrupts (`firmware/components/input/buttons.c`). When the
screen still answers, Settings has Restart.
To select recovery at the next reset, keep BACK held for the bootloader's three-second
entry window. Recovery itself reads only OK and BACK; it does not run the main
firmware's ten-second rescue combination.
